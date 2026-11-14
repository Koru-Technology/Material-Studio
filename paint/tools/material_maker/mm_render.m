// Renders a Material Maker plan with Metal (the checker's conformance renders), binding like the app's Metal backend:
// vertex buffer 0, argument buffer at [[buffer(1)]] with constants (0), sampler (1) and textures (2 + i).

#import <Metal/Metal.h>
#include "../../sources/material_maker/mm.h"
#include "../../../base/sources/iron_glsl.h"
#include <string.h>

#define MAX_TEXTURES 32

static id<MTLDevice>          device;
static id<MTLCommandQueue>    queue;
static id<MTLSamplerState>    sampler;
static id<MTLArgumentEncoder> encoder;
static id<MTLBuffer>          quad;

static void init(void) {
	if (device != nil) {
		return;
	}
	device                          = MTLCreateSystemDefaultDevice();
	queue                           = [device newCommandQueue];
	MTLSamplerDescriptor *sd        = [MTLSamplerDescriptor new];
	sd.minFilter                    = MTLSamplerMinMagFilterLinear;
	sd.magFilter                    = MTLSamplerMinMagFilterLinear;
	sd.mipFilter                    = MTLSamplerMipFilterNotMipmapped;
	sd.sAddressMode                 = MTLSamplerAddressModeRepeat;
	sd.tAddressMode                 = MTLSamplerAddressModeRepeat;
	sd.supportArgumentBuffers       = true;
	sampler                         = [device newSamplerStateWithDescriptor:sd];
	MTLArgumentDescriptor *cd       = [MTLArgumentDescriptor argumentDescriptor];
	cd.dataType                     = MTLDataTypePointer;
	cd.index                        = 0;
	MTLArgumentDescriptor *smd      = [MTLArgumentDescriptor argumentDescriptor];
	smd.dataType                    = MTLDataTypeSampler;
	smd.index                       = 1;
	NSMutableArray *args            = [NSMutableArray arrayWithObjects:cd, smd, nil];
	for (int i = 0; i < MAX_TEXTURES; ++i) {
		MTLArgumentDescriptor *td = [MTLArgumentDescriptor argumentDescriptor];
		td.dataType               = MTLDataTypeTexture;
		td.index                  = i + 2;
		td.textureType            = MTLTextureType2D;
		[args addObject:td];
	}
	encoder           = [device newArgumentEncoderWithArguments:args];
	float tri[]       = {-1, -1, 3, -1, -1, 3};
	quad              = [device newBufferWithBytes:tri length:sizeof(tri) options:MTLResourceStorageModeShared];
}

static id<MTLRenderPipelineState> pipeline(const char *glsl, MTLPixelFormat format, char **error) {
	char *e  = NULL;
	char *vs = iron_glsl_to_msl(glsl, IRON_GLSL_VERTEX, NULL, false, &e);
	if (vs == NULL) {
		*error = strdup(e != NULL ? e : "vertex");
		return nil;
	}
	NSString *vsrc = [NSString stringWithUTF8String:vs];
	char     *fs   = iron_glsl_to_msl(glsl, IRON_GLSL_FRAGMENT, NULL, false, &e);
	if (fs == NULL) {
		*error = strdup(e != NULL ? e : "fragment");
		return nil;
	}
	NSString *fsrc = [NSString stringWithUTF8String:fs];
	NSError  *err  = nil;
	MTLCompileOptions *opts = [MTLCompileOptions new];
	// Fast math, as the app and Material Maker (MoltenVK) compile. Material Maker's hashes (fract(cos(x) * 43758.5)...)
	// are sensitive to it: the other modes give other noises. MM_MATH=fast|relaxed|safe to compare.
	const char *mode = getenv("MM_MATH") != NULL ? getenv("MM_MATH") : "fast";
	if (@available(macOS 15.0, *)) {
		opts.mathMode = strcmp(mode, "fast") == 0 ? MTLMathModeFast : strcmp(mode, "relaxed") == 0 ? MTLMathModeRelaxed : MTLMathModeSafe;
	}
	else {
		opts.fastMathEnabled = strcmp(mode, "safe") != 0;
	}
	id<MTLLibrary> vl = [device newLibraryWithSource:vsrc options:opts error:&err];
	id<MTLLibrary> fl = vl != nil ? [device newLibraryWithSource:fsrc options:opts error:&err] : nil;
	if (vl == nil || fl == nil) {
		*error = strdup(err.localizedDescription.UTF8String);
		return nil;
	}
	MTLRenderPipelineDescriptor *pd   = [MTLRenderPipelineDescriptor new];
	pd.vertexFunction                 = [vl newFunctionWithName:@"vert"];
	pd.fragmentFunction               = [fl newFunctionWithName:@"frag"];
	pd.colorAttachments[0].pixelFormat = format;
	MTLVertexDescriptor *vd           = [MTLVertexDescriptor vertexDescriptor];
	vd.attributes[0].format           = MTLVertexFormatFloat2;
	vd.attributes[0].offset           = 0;
	vd.attributes[0].bufferIndex      = 0;
	vd.layouts[0].stride              = 8;
	vd.layouts[0].stepFunction        = MTLVertexStepFunctionPerVertex;
	pd.vertexDescriptor               = vd;
	id<MTLRenderPipelineState> ps     = [device newRenderPipelineStateWithDescriptor:pd error:&err];
	if (ps == nil) {
		*error = strdup(err.localizedDescription.UTF8String);
	}
	return ps;
}

static MTLPixelFormat format_of(mm_pass_t *p) {
	if (p->greyscale) {
		return p->f32 ? MTLPixelFormatR32Float : MTLPixelFormatR16Float;
	}
	return p->f32 ? MTLPixelFormatRGBA32Float : MTLPixelFormatRGBA16Float;
}

static id<MTLTexture> new_target(MTLPixelFormat format, int size) {
	MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:size height:size mipmapped:NO];
	td.usage                 = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
	td.storageMode           = MTLStorageModePrivate;
	return [device newTextureWithDescriptor:td];
}

static void draw(id<MTLRenderPipelineState> ps, id<MTLTexture> target, NSArray<id<MTLTexture>> *textures, float size, float iteration) {
	id<MTLCommandBuffer>     cb  = [queue commandBuffer];
	MTLRenderPassDescriptor *rp  = [MTLRenderPassDescriptor renderPassDescriptor];
	rp.colorAttachments[0].texture     = target;
	rp.colorAttachments[0].loadAction  = MTLLoadActionClear;
	rp.colorAttachments[0].clearColor  = MTLClearColorMake(0, 0, 0, 0);
	rp.colorAttachments[0].storeAction = MTLStoreActionStore;
	id<MTLRenderCommandEncoder> ce = [cb renderCommandEncoderWithDescriptor:rp];
	if (ps == nil) { // Did not compile: Material Maker leaves the texture empty
		[ce endEncoding];
		[cb commit];
		[cb waitUntilCompleted];
		return;
	}
	[ce setRenderPipelineState:ps];
	float           c[4]  = {size, iteration, 1.0f, 0}; // z: one, a run time 1.0 (mm_uniform_runtime_declaration)
	id<MTLBuffer>   cbuf  = [device newBufferWithBytes:c length:256 > sizeof(c) ? sizeof(c) : 256 options:MTLResourceStorageModeShared];
	id<MTLBuffer>   abuf  = [device newBufferWithLength:encoder.encodedLength options:MTLResourceStorageModeShared];
	[encoder setArgumentBuffer:abuf offset:0];
	[encoder setBuffer:cbuf offset:0 atIndex:0];
	[encoder setSamplerState:sampler atIndex:1];
	[ce useResource:cbuf usage:MTLResourceUsageRead stages:MTLRenderStageVertex | MTLRenderStageFragment];
	for (NSUInteger i = 0; i < textures.count; ++i) {
		[encoder setTexture:textures[i] atIndex:i + 2];
		[ce useResource:textures[i] usage:MTLResourceUsageRead stages:MTLRenderStageVertex | MTLRenderStageFragment];
	}
	[ce setVertexBuffer:quad offset:0 atIndex:0];
	[ce setVertexBuffer:abuf offset:0 atIndex:1];
	[ce setFragmentBuffer:abuf offset:0 atIndex:1];
	[ce drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
	[ce endEncoding];
	[cb commit];
	[cb waitUntilCompleted];
}

static void render_buffers_of_loop(mm_plan_t *plan, int it, NSMutableArray *results, id<MTLRenderPipelineState> __strong *pipes);

static id<MTLTexture> empty_texture(void) {
	static id<MTLTexture> t;
	if (t == nil) {
		MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:4 height:4 mipmapped:NO];
		td.usage                 = MTLTextureUsageShaderRead;
		td.storageMode           = MTLStorageModeShared;
		t                        = [device newTextureWithDescriptor:td];
		uint16_t zero[64]        = {0};
		[t replaceRegion:MTLRegionMake2D(0, 0, 4, 4) mipmapLevel:0 withBytes:zero bytesPerRow:32];
	}
	return t;
}

static NSArray *inputs(mm_plan_t *plan, int n, int *passes, NSMutableArray *results, id<MTLTexture> self_prev) {
	NSMutableArray *a = [NSMutableArray array];
	for (int i = 0; i < n; ++i) {
		id t = passes[i] < 0 ? self_prev : results[passes[i]];
		[a addObject:(t != nil && t != [NSNull null]) ? t : empty_texture()];
	}
	return a;
}

static id<MTLTexture> render_pass(mm_plan_t *plan, int i, NSMutableArray *results, id<MTLRenderPipelineState> __strong *pipes,
                                  id<MTLRenderPipelineState> __strong *loop_pipes) {
	mm_pass_t     *p  = &plan->passes[i];
	if (p->texture_gen != NULL) { // Image or text node: its pixels as RGBA8 unorm
		int      w, h;
		uint8_t *rgba = NULL;
		mm_texture_pixels(p->texture_gen, &w, &h, &rgba);
		MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:w height:h mipmapped:NO];
		td.usage                 = MTLTextureUsageShaderRead;
		td.storageMode           = MTLStorageModeShared;
		id<MTLTexture> tex       = [device newTextureWithDescriptor:td];
		[tex replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0 withBytes:rgba bytesPerRow:w * 4];
		free(rgba);
		return tex;
	}
	MTLPixelFormat f  = format_of(p);
	id<MTLTexture> t  = new_target(f, p->size);
	draw(pipes[i], t, inputs(plan, p->textures, p->texture_pass, results, nil), p->size, 0);
	if (p->iterate) {
		for (int k = 1; k <= p->iterations; ++k) {
			results[i] = t; // The loop's buffers and loop source read the previous iteration
			for (int j = 0; j < plan->count; ++j) {
				if (plan->passes[j].loop_of == i) {
					results[j] = render_pass(plan, j, results, pipes, loop_pipes);
				}
			}
			int size = p->size;
			if (p->shrink) {
				size = size >> k;
				size = size < 4 ? 4 : size;
			}
			id<MTLTexture> n = new_target(f, size);
			draw(loop_pipes[i], n, inputs(plan, p->loop_textures, p->loop_texture_pass, results, t), size, k);
			t = n;
		}
	}
	return t;
}

// maps[m]: size*size*4 floats for each map with a pass, NULL otherwise. Passes that do not compile render empty, as in
// Material Maker; *failed counts them.
bool mm_render(mm_plan_t *plan, float **maps, int *failed) {
	@autoreleasepool {
		init();
		id<MTLRenderPipelineState> __strong *pipes      = (id<MTLRenderPipelineState> __strong *)calloc(plan->count, sizeof(id));
		id<MTLRenderPipelineState> __strong *loop_pipes = (id<MTLRenderPipelineState> __strong *)calloc(plan->count, sizeof(id));
		bool ok = true;
		*failed = 0;
		for (int i = 0; i < plan->count; ++i) {
			mm_pass_t *p     = &plan->passes[i];
			char      *error = NULL;
			if (p->texture_gen != NULL) { // CPU texture: uploaded in render_pass
				continue;
			}
			pipes[i]         = pipeline(p->glsl, format_of(p), &error);
			*failed += pipes[i] == nil;
			free(error);
			if (p->iterate) {
				error         = NULL;
				loop_pipes[i] = pipeline(p->glsl_loop, format_of(p), &error);
				*failed += loop_pipes[i] == nil;
				free(error);
			}
		}
		{
			NSMutableArray *results = [NSMutableArray array];
			for (int i = 0; i < plan->count; ++i) {
				[results addObject:[NSNull null]];
			}
			for (int i = 0; i < plan->count; ++i) {
				if (plan->passes[i].loop_of < 0) {
					results[i] = render_pass(plan, i, results, pipes, loop_pipes);
				}
			}
			for (int m = 0; m < MM_MAP_COUNT; ++m) {
				int pi = plan->map_pass[m];
				if (pi < 0 || maps[m] == NULL) {
					continue;
				}
				// Read back through an RGBA32F copy
				mm_pass_t     *p   = &plan->passes[pi];
				id<MTLTexture> src = results[pi];
				MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:p->size height:p->size mipmapped:NO];
				td.usage                 = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
				td.storageMode           = MTLStorageModeShared;
				id<MTLTexture> dst       = [device newTextureWithDescriptor:td];
				static id<MTLRenderPipelineState> copy_grey, copy_rgba;
				const char *copy_src     = "#version 450\nlayout(set = 0, binding = 0, std140) uniform Constants { vec4 mm; } constants;\n"
				                           "layout(set = 0, binding = 1) uniform sampler sampler_linear;\n"
				                           "layout(set = 0, binding = 2) uniform texture2D src;\n"
				                           "#ifdef VERT\nlayout(location = 0) in vec2 in_pos;\nvoid main() { gl_Position = vec4(in_pos, 0.0, 1.0); }\n#endif\n"
				                           "#ifdef FRAG\nlayout(location = 0) out vec4 frag_color;\n"
				                           "void main() { vec4 v = texelFetch(sampler2D(src, sampler_linear), ivec2(gl_FragCoord.xy), 0); frag_color = constants.mm.z > 0.5 ? vec4(v.rrr, 1.0) : v; }\n#endif\n";
				if (copy_rgba == nil) {
					char *error = NULL;
					copy_rgba   = pipeline(copy_src, MTLPixelFormatRGBA32Float, &error);
					if (copy_rgba == nil) {
						fprintf(stderr, "copy shader: %s\n", error);
						exit(1);
					}
				}
				(void)copy_grey;
				id<MTLCommandBuffer>     cb = [queue commandBuffer];
				MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
				rp.colorAttachments[0].texture     = dst;
				rp.colorAttachments[0].loadAction  = MTLLoadActionDontCare;
				rp.colorAttachments[0].storeAction = MTLStoreActionStore;
				id<MTLRenderCommandEncoder> ce     = [cb renderCommandEncoderWithDescriptor:rp];
				[ce setRenderPipelineState:copy_rgba];
				float         c[4] = {p->size, 0, p->greyscale ? 1.0f : 0.0f, 0};
				id<MTLBuffer> cbuf = [device newBufferWithBytes:c length:sizeof(c) options:MTLResourceStorageModeShared];
				id<MTLBuffer> abuf = [device newBufferWithLength:encoder.encodedLength options:MTLResourceStorageModeShared];
				[encoder setArgumentBuffer:abuf offset:0];
				[encoder setBuffer:cbuf offset:0 atIndex:0];
				[encoder setSamplerState:sampler atIndex:1];
				[encoder setTexture:src atIndex:2];
				[ce useResource:cbuf usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
				[ce useResource:src usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
				[ce setVertexBuffer:quad offset:0 atIndex:0];
				[ce setVertexBuffer:abuf offset:0 atIndex:1];
				[ce setFragmentBuffer:abuf offset:0 atIndex:1];
				[ce drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
				[ce endEncoding];
				[cb commit];
				[cb waitUntilCompleted];
				[dst getBytes:maps[m] bytesPerRow:p->size * 16 fromRegion:MTLRegionMake2D(0, 0, p->size, p->size) mipmapLevel:0];
			}
		}
		for (int i = 0; i < plan->count; ++i) {
			pipes[i]      = nil;
			loop_pipes[i] = nil;
		}
		free(pipes);
		free(loop_pipes);
		return ok;
	}
}
