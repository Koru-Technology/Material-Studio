// Hardware raytracing (path tracer and raytraced bakes) of the Metal backend, part of metal_gpu.m: included there,
// it uses the command buffer and encoder of that file

typedef struct inst {
	mat4_t m;
	int    i;
} inst_t;

typedef struct rt_instance_data {
	uint64_t vertex_buffer; // GPU addresses
	uint64_t index_buffer;
	uint32_t stride;
	uint32_t geometry;
} rt_instance_data_t;

static gpu_acceleration_structure_t *accel;
static gpu_raytrace_pipeline_t      *pipeline;
static gpu_texture_t                *output = NULL;
static gpu_buffer_t                 *constant_buf;
static id<MTLComputePipelineState>   _raytracing_pipeline;
static NSMutableArray               *_primitive_accels;
static id<MTLAccelerationStructure>  _instance_accel;
static dispatch_semaphore_t          _semaphore;
static gpu_texture_t                *_texpaint0;
static gpu_texture_t                *_texpaint1;
static gpu_texture_t                *_texpaint2;
static gpu_texture_t                *_texenv;
static gpu_texture_t                *_texsobol;
static gpu_texture_t                *_texscramble;
static gpu_texture_t                *_texrank;
static gpu_texture_t                *_texenv_cdf;
static gpu_texture_t                *geometry_tex[GPU_RAYTRACE_MAX_OBJECTS][3]; // NULL uses the shared textures
static id<MTLBuffer>                 _geometry_textures = nil;
static gpu_buffer_t                 *vb[GPU_RAYTRACE_MAX_OBJECTS];
static gpu_buffer_t                 *vb_last[GPU_RAYTRACE_MAX_OBJECTS];
static uint32_t                      vb_version_last[GPU_RAYTRACE_MAX_OBJECTS];
static uint32_t                      ib_version_last[GPU_RAYTRACE_MAX_OBJECTS];
static gpu_buffer_t                 *ib[GPU_RAYTRACE_MAX_OBJECTS];
static int                           vb_count      = 0;
static int                           vb_count_last = 0;
static inst_t                        instances[1024];
static int                           instances_count = 0;
static id<MTLBuffer>                 _instance_data  = nil;

void gpu_raytrace_pipeline_init(gpu_raytrace_pipeline_t *pipeline, void *shader, int ray_shader_size, gpu_buffer_t *constant_buffer) {
	id<MTLDevice> device = get_metal_device();
	if (!device.supportsRaytracing)
		return;
	constant_buf = constant_buffer;

	NSError       *error   = nil;
	id<MTLLibrary> library = [device newLibraryWithSource:[[NSString alloc] initWithBytes:shader length:ray_shader_size encoding:NSUTF8StringEncoding]
	                                              options:nil
	                                                error:&error];
	if (library == nil) {
		iron_error("%s", error.localizedDescription.UTF8String);
	}

	MTLComputePipelineDescriptor *descriptor                   = [[MTLComputePipelineDescriptor alloc] init];
	descriptor.computeFunction                                 = [library newFunctionWithName:@"raytrace"];
	descriptor.threadGroupSizeIsMultipleOfThreadExecutionWidth = YES;
	_raytracing_pipeline = [device newComputePipelineStateWithDescriptor:descriptor options:0 reflection:nil error:&error];
	_semaphore           = dispatch_semaphore_create(2);
}

void gpu_raytrace_pipeline_destroy(gpu_raytrace_pipeline_t *pipeline) {}

bool gpu_raytrace_supported() {
	id<MTLDevice> device = get_metal_device();
	return device.supportsRaytracing;
}

id<MTLAccelerationStructure> create_acceleration_sctructure(MTLAccelerationStructureDescriptor *descriptor) {
	id<MTLDevice>       device = get_metal_device();
	id<MTLCommandQueue> queue  = get_metal_queue();

	MTLAccelerationStructureSizes accel_sizes            = [device accelerationStructureSizesWithDescriptor:descriptor];
	id<MTLAccelerationStructure>  acceleration_structure = [device newAccelerationStructureWithSize:accel_sizes.accelerationStructureSize];

	id<MTLBuffer>        scratch_buffer = [device newBufferWithLength:accel_sizes.buildScratchBufferSize options:MTLResourceStorageModePrivate];
	id<MTLCommandBuffer> command_buffer = [queue commandBuffer];
	id<MTLAccelerationStructureCommandEncoder> command_encoder        = [command_buffer accelerationStructureCommandEncoder];
	id<MTLBuffer>                              compacteds_size_buffer = [device newBufferWithLength:sizeof(uint32_t) options:MTLResourceStorageModeShared];

	[command_encoder buildAccelerationStructure:acceleration_structure descriptor:descriptor scratchBuffer:scratch_buffer scratchBufferOffset:0];

	[command_encoder writeCompactedAccelerationStructureSize:acceleration_structure toBuffer:compacteds_size_buffer offset:0];

	[command_encoder endEncoding];
	[command_buffer commit];
	[command_buffer waitUntilCompleted];

	uint32_t                     compacted_size                   = *(uint32_t *)compacteds_size_buffer.contents;
	id<MTLAccelerationStructure> compacted_acceleration_structure = [device newAccelerationStructureWithSize:compacted_size];
	command_buffer                                                = [queue commandBuffer];
	command_encoder                                               = [command_buffer accelerationStructureCommandEncoder];
	[command_encoder copyAndCompactAccelerationStructure:acceleration_structure toAccelerationStructure:compacted_acceleration_structure];
	[command_encoder endEncoding];
	[command_buffer commit];

	return compacted_acceleration_structure;
}

void gpu_raytrace_acceleration_structure_init(gpu_acceleration_structure_t *accel) {
	vb_count        = 0;
	instances_count = 0;
	memset(vb, 0, sizeof(vb));
	memset(geometry_tex, 0, sizeof(geometry_tex));
}

void gpu_raytrace_acceleration_structure_add(gpu_acceleration_structure_t *accel, gpu_buffer_t *_vb, gpu_buffer_t *_ib, mat4_t _transform,
                                             gpu_texture_t **textures) {

	int vb_i = -1;
	for (int i = 0; i < vb_count; ++i) {
		if (_vb == vb[i]) {
			vb_i = i;
			break;
		}
	}
	if (vb_i == -1) {
		if (vb_count >= GPU_RAYTRACE_MAX_OBJECTS) {
			return;
		}
		vb_i         = vb_count;
		vb[vb_count] = _vb;
		ib[vb_count] = _ib;
		for (int k = 0; k < 3; ++k) {
			geometry_tex[vb_count][k] = textures != NULL ? textures[k] : NULL;
		}
		vb_count++;
	}

	if (instances_count >= (int)ARRAY_LENGTH(instances)) {
		return;
	}

	inst_t inst                = {.i = vb_i, .m = _transform};
	instances[instances_count] = inst;
	instances_count++;
}

void _gpu_raytrace_acceleration_structure_destroy_bottom(gpu_acceleration_structure_t *accel) {
	//	for (int i = 0; i < vb_count_last; ++i) {
	//	}
	_primitive_accels = nil;
}

void _gpu_raytrace_acceleration_structure_destroy_top(gpu_acceleration_structure_t *accel) {
	_instance_accel = nil;
	_instance_data  = nil;
}

void gpu_raytrace_acceleration_structure_build(gpu_acceleration_structure_t *accel) {
	gpu_execute_and_wait();

	bool build_bottom = false;
	for (int i = 0; i < GPU_RAYTRACE_MAX_OBJECTS; ++i) {
		uint32_t vb_version = vb[i] != NULL ? vb[i]->version : 0;
		uint32_t ib_version = vb[i] != NULL ? ib[i]->version : 0;
		if (vb_last[i] != vb[i] || vb_version_last[i] != vb_version || ib_version_last[i] != ib_version) {
			build_bottom = true;
		}
		vb_last[i]         = vb[i];
		vb_version_last[i] = vb_version;
		ib_version_last[i] = ib_version;
	}

	if (vb_count_last > 0) {
		if (build_bottom) {
			_gpu_raytrace_acceleration_structure_destroy_bottom(accel);
		}
		_gpu_raytrace_acceleration_structure_destroy_top(accel);
	}

	vb_count_last = vb_count;

	if (vb_count == 0) {
		return;
	}

	id<MTLDevice> device = get_metal_device();
	if (!device.supportsRaytracing) {
		return;
	}

	MTLResourceOptions options = MTLResourceStorageModeShared;

	// Bottom level
	if (build_bottom || _primitive_accels == nil) {
		_primitive_accels = [[NSMutableArray alloc] init];
		for (int i = 0; i < vb_count; ++i) {
			MTLAccelerationStructureTriangleGeometryDescriptor *descriptor = [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];
			descriptor.indexType                                           = MTLIndexTypeUInt32;
			descriptor.indexBuffer                                         = (__bridge id<MTLBuffer>)ib[i]->impl.metal_buffer;
			descriptor.vertexBuffer                                        = (__bridge id<MTLBuffer>)vb[i]->impl.metal_buffer;
			descriptor.vertexStride                                        = vb[i]->stride;
			descriptor.triangleCount                                       = ib[i]->count / 3;
			descriptor.vertexFormat                                        = MTLAttributeFormatShort4Normalized;

			MTLPrimitiveAccelerationStructureDescriptor *accel_descriptor = [MTLPrimitiveAccelerationStructureDescriptor descriptor];
			accel_descriptor.geometryDescriptors                          = @[ descriptor ];
			[_primitive_accels addObject:create_acceleration_sctructure(accel_descriptor)];
		}
	}

	// Top level
	int instance_count = instances_count;

	id<MTLBuffer> instance_buffer = [device newBufferWithLength:sizeof(MTLAccelerationStructureUserIDInstanceDescriptor) * instance_count options:options];
	MTLAccelerationStructureUserIDInstanceDescriptor *instance_descriptors = (MTLAccelerationStructureUserIDInstanceDescriptor *)instance_buffer.contents;

	for (int i = 0; i < instance_count; ++i) {
		float *m = instances[i].m.m;

		instance_descriptors[i].accelerationStructureIndex      = instances[i].i;
		instance_descriptors[i].options                         = MTLAccelerationStructureInstanceOptionOpaque;
		instance_descriptors[i].mask                            = 1;
		instance_descriptors[i].intersectionFunctionTableOffset = 0;
		instance_descriptors[i].transformationMatrix.columns[0] = MTLPackedFloat3Make(m[0], m[1], m[2]);
		instance_descriptors[i].transformationMatrix.columns[1] = MTLPackedFloat3Make(m[4], m[5], m[6]);
		instance_descriptors[i].transformationMatrix.columns[2] = MTLPackedFloat3Make(m[8], m[9], m[10]);
		instance_descriptors[i].transformationMatrix.columns[3] = MTLPackedFloat3Make(m[12], m[13], m[14]);

		instance_descriptors[i].userID = i;
	}

	MTLInstanceAccelerationStructureDescriptor *inst_accel_descriptor = [MTLInstanceAccelerationStructureDescriptor descriptor];
	inst_accel_descriptor.instancedAccelerationStructures             = _primitive_accels;
	inst_accel_descriptor.instanceCount                               = instance_count;
	inst_accel_descriptor.instanceDescriptorBuffer                    = instance_buffer;
	inst_accel_descriptor.instanceDescriptorType                      = MTLAccelerationStructureInstanceDescriptorTypeUserID;
	_instance_accel                                                   = create_acceleration_sctructure(inst_accel_descriptor);

	_instance_data           = [device newBufferWithLength:sizeof(rt_instance_data_t) * instances_count options:options];
	rt_instance_data_t *data = (rt_instance_data_t *)_instance_data.contents;
	for (int i = 0; i < instances_count; ++i) {
		inst_t *inst          = &instances[i];
		data[i].vertex_buffer = ((__bridge id<MTLBuffer>)vb[inst->i]->impl.metal_buffer).gpuAddress;
		data[i].index_buffer  = ((__bridge id<MTLBuffer>)ib[inst->i]->impl.metal_buffer).gpuAddress;
		data[i].stride        = vb[inst->i]->stride;
		data[i].geometry      = inst->i;
	}
}

void gpu_raytrace_acceleration_structure_destroy(gpu_acceleration_structure_t *accel) {}

void gpu_raytrace_set_textures(gpu_texture_t *texpaint0, gpu_texture_t *texpaint1, gpu_texture_t *texpaint2, gpu_texture_t *texenv, gpu_texture_t *texsobol,
                               gpu_texture_t *texscramble, gpu_texture_t *texrank, gpu_texture_t *texenv_cdf) {
	_texpaint0   = texpaint0;
	_texpaint1   = texpaint1;
	_texpaint2   = texpaint2;
	_texenv      = texenv;
	_texsobol    = texsobol;
	_texscramble = texscramble;
	_texrank     = texrank;
	_texenv_cdf  = texenv_cdf != NULL ? texenv_cdf : texenv;
}

void gpu_raytrace_set_acceleration_structure(gpu_acceleration_structure_t *_accel) {
	accel = _accel;
}

void gpu_raytrace_set_pipeline(gpu_raytrace_pipeline_t *_rt_pipeline) {
	pipeline = _rt_pipeline;
}

// Render targets are created without ShaderWrite, the first ray dispatch into one swaps in a writable copy
static void gpu_make_shader_writable(gpu_texture_t *target) {
	id<MTLTexture> old = (__bridge id<MTLTexture>)target->impl._tex;
	if (old == nil || (old.usage & MTLTextureUsageShaderWrite)) {
		return;
	}
	MTLTextureDescriptor *descriptor = [MTLTextureDescriptor new];
	descriptor.textureType           = MTLTextureType2D;
	descriptor.width                 = old.width;
	descriptor.height                = old.height;
	descriptor.depth                 = 1;
	descriptor.pixelFormat           = old.pixelFormat;
	descriptor.arrayLength           = 1;
	descriptor.mipmapLevelCount      = 1;
	descriptor.usage                 = old.usage | MTLTextureUsageShaderWrite;
	descriptor.resourceOptions       = MTLResourceStorageModePrivate;
	id<MTLTexture> tex               = [get_metal_device() newTextureWithDescriptor:descriptor];
	if (tex == nil) {
		return;
	}
	id<MTLCommandBuffer>      command_buffer = [get_metal_queue() commandBuffer];
	id<MTLBlitCommandEncoder> blit           = [command_buffer blitCommandEncoder];
	[blit copyFromTexture:old toTexture:tex];
	[blit endEncoding];
	[command_buffer commit];
	[command_buffer waitUntilCompleted];
	id<MTLTexture> released = (__bridge_transfer id<MTLTexture>)target->impl._tex; // The dispatch waited for the gpu, nothing uses it
	released                = nil;
	target->impl._tex       = (__bridge_retained void *)tex;
}

void gpu_raytrace_set_target(gpu_texture_t *_output) {
	output = _output;
	gpu_make_shader_writable(output);
}

void gpu_raytrace_dispatch_rays() {
	id<MTLDevice> device = get_metal_device();
	if (!device.supportsRaytracing)
		return;
	if (_instance_accel == nil || _instance_data == nil)
		return;
	dispatch_semaphore_wait(_semaphore, DISPATCH_TIME_FOREVER);

	id<MTLCommandQueue>          queue          = get_metal_queue();
	id<MTLCommandBuffer>         command_buffer = [queue commandBuffer];
	__block dispatch_semaphore_t sem            = _semaphore;
	[command_buffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
		dispatch_semaphore_signal(sem);
	}];

	NSUInteger width                   = output->width;
	NSUInteger height                  = output->height;
	MTLSize    threads_per_threadgroup = MTLSizeMake(8, 8, 1);
	MTLSize    threadgroups            = MTLSizeMake((width + threads_per_threadgroup.width - 1) / threads_per_threadgroup.width,
	                                                 (height + threads_per_threadgroup.height - 1) / threads_per_threadgroup.height, 1);

	id<MTLComputeCommandEncoder> compute_encoder = [command_buffer computeCommandEncoder];
	[compute_encoder setBuffer:(__bridge id<MTLBuffer>)constant_buf->impl.metal_buffer offset:0 atIndex:0];
	[compute_encoder setAccelerationStructure:_instance_accel atBufferIndex:1];
	[compute_encoder setBuffer:_instance_data offset:0 atIndex:2];

	if (_geometry_textures == nil) {
		_geometry_textures = [device newBufferWithLength:sizeof(MTLResourceID) * GPU_RAYTRACE_MAX_OBJECTS * 3 options:MTLResourceStorageModeShared];
	}
	gpu_texture_t *shared_tex[3] = {_texpaint0, _texpaint1, _texpaint2};
	MTLResourceID *texture_ids   = (MTLResourceID *)_geometry_textures.contents;
	for (int i = 0; i < GPU_RAYTRACE_MAX_OBJECTS; ++i) {
		int g = i < vb_count ? i : 0;
		for (int k = 0; k < 3; ++k) {
			gpu_texture_t *tex     = geometry_tex[g][k] != NULL ? geometry_tex[g][k] : shared_tex[k];
			id<MTLTexture> mtl_tex = (__bridge id<MTLTexture>)tex->impl._tex;
			texture_ids[i * 3 + k] = mtl_tex.gpuResourceID;
			if (i < vb_count) {
				[compute_encoder useResource:mtl_tex usage:MTLResourceUsageRead];
			}
		}
	}
	[compute_encoder setBuffer:_geometry_textures offset:0 atIndex:3];
	[compute_encoder setTexture:(__bridge id<MTLTexture>)output->impl._tex atIndex:0];
	[compute_encoder setTexture:(__bridge id<MTLTexture>)_texpaint0->impl._tex atIndex:1];
	[compute_encoder setTexture:(__bridge id<MTLTexture>)_texpaint1->impl._tex atIndex:2];
	[compute_encoder setTexture:(__bridge id<MTLTexture>)_texpaint2->impl._tex atIndex:3];
	[compute_encoder setTexture:(__bridge id<MTLTexture>)_texenv->impl._tex atIndex:4];
	[compute_encoder setTexture:(__bridge id<MTLTexture>)_texsobol->impl._tex atIndex:5];
	[compute_encoder setTexture:(__bridge id<MTLTexture>)_texscramble->impl._tex atIndex:6];
	[compute_encoder setTexture:(__bridge id<MTLTexture>)_texrank->impl._tex atIndex:7];
	[compute_encoder setTexture:(__bridge id<MTLTexture>)_texenv_cdf->impl._tex atIndex:8];
	[compute_encoder setSamplerState:linear_sampler atIndex:0];

	for (id<MTLAccelerationStructure> primitive_accel in _primitive_accels) {
		[compute_encoder useResource:primitive_accel usage:MTLResourceUsageRead];
	}
	for (int i = 0; i < vb_count; ++i) {
		[compute_encoder useResource:(__bridge id<MTLBuffer>)vb[i]->impl.metal_buffer usage:MTLResourceUsageRead];
		[compute_encoder useResource:(__bridge id<MTLBuffer>)ib[i]->impl.metal_buffer usage:MTLResourceUsageRead];
	}

	[compute_encoder setComputePipelineState:_raytracing_pipeline];
	[compute_encoder dispatchThreadgroups:threadgroups threadsPerThreadgroup:threads_per_threadgroup];
	[compute_encoder endEncoding];
	[command_buffer commit];
}
