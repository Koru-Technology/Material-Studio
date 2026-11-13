#include "amake.h"

project_t *main() {
	project_t *project = project_create("amake");
	add_define(project, "AMAKE");

	add_include_dir(project, "../../sources");
	add_cfiles(project, "../../sources/iron_string.c");
	add_cfiles(project, "../../sources/iron_array.c");
	add_cfiles(project, "../../sources/libs/minic.c");

	add_cfiles(project, "main.c");
	add_cfiles(project, "make.c");
	add_cfiles(project, "exporters.c");
	add_cfiles(project, "script.c");
	add_cfiles(project, "util.c");
	add_cfiles(project, "aimage.c");
	add_cfiles(project, "ashader.c");

	add_cfiles(project, "../../sources/iron_dir.c");

	// GLSL shaders (iron_glsl.c): glslang and SPIRV-Cross
	add_define(project, "WITH_GLSL");
	add_define(project, "SPIRV_CROSS_C_API_MSL=1");
	add_define(project, "SPIRV_CROSS_C_API_GLSL=1");
	add_include_dir(project, "../../sources/libs/glslang");
	add_include_dir(project, "../../sources/libs/spirv_cross");
	add_cfiles(project, "../../sources/iron_glsl.c");
	add_cfiles(project, "../../sources/libs/glslang/glslang/MachineIndependent/*.cpp");
	add_cfiles(project, "../../sources/libs/glslang/glslang/MachineIndependent/preprocessor/*.cpp");
	add_cfiles(project, "../../sources/libs/glslang/glslang/GenericCodeGen/*.cpp");
	add_cfiles(project, "../../sources/libs/glslang/glslang/OSDependent/Unix/ossource.cpp");
	add_cfiles(project, "../../sources/libs/glslang/glslang/CInterface/*.cpp");
	add_cfiles(project, "../../sources/libs/glslang/glslang/ResourceLimits/*.cpp");
	add_cfiles(project, "../../sources/libs/glslang/SPIRV/*.cpp");
	add_cfiles(project, "../../sources/libs/glslang/SPIRV/CInterface/*.cpp");
	add_cfiles(project, "../../sources/libs/spirv_cross/*.cpp");

	if (platform == PLATFORM_WINDOWS) {
		add_define(project, "_CRT_SECURE_NO_WARNINGS");
		add_lib(project, "d3dcompiler");
		add_lib(project, "dxguid");
	}
	else if (platform == PLATFORM_MACOS) {
		add_cfiles(project, "../../sources/backends/data/mac.plist");
	}
	else if (platform == PLATFORM_LINUX) {
		add_lib(project, "dl -static");
	}

	return project;
}
