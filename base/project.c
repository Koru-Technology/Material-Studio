#include "tools/amake/amake.h"

project_t *main() {
	project_t *project = project_create("Base");
	add_include_dir(project, "sources");
	add_include_dir(project, "sources/libs");
	add_shaders(project, "shaders/*.shader");
	add_shaders(project, "shaders/*.glsl");
	add_assets(project, "assets/*", "data/{name}", 0);
	add_assets(project, "assets/licenses/**", "data/licenses/{name}", 0);
	add_assets(project, "assets/themes/*.json", "data/themes/{name}", 0);
	add_cfiles(project, "sources/*.c");
	add_define(project, string("EMBED_H_PATH=\"%s/build/embed.h\"", flags->dirname));

	// Mac and iPad, Metal
	if (platform == PLATFORM_MACOS) {
		add_cfiles(project, "sources/backends/macos_system.*");
		add_cfiles(project, "sources/backends/apple_system.m");
		add_cfiles(project, "sources/backends/apple_net.*");
		add_cfiles(project, "sources/backends/apple_thread.*");
		add_cfiles(project, "sources/backends/metal_gpu.*");
		add_cfiles(project, "sources/backends/data/mac.plist");
		add_lib(project, "CoreText");
		add_define(project, "IRON_METAL");
	}
	else if (platform == PLATFORM_IOS) {
		add_cfiles(project, "sources/backends/ios_system.*");
		add_cfiles(project, "sources/backends/apple_system.m");
		add_cfiles(project, "sources/backends/apple_net.*");
		add_cfiles(project, "sources/backends/apple_thread.*");
		add_cfiles(project, "sources/backends/metal_gpu.*");
		add_cfiles(project, "sources/backends/data/ios.plist");
		add_cfiles(project, "sources/backends/ios_file_dialog.m");
		add_cfiles(project, "sources/backends/ios_folder.m");
		add_lib(project, "CoreText");
		if (flags->with_audio) {
			add_cfiles(project, "sources/backends/ios_audio.m");
			add_lib(project, "AudioToolbox");
			add_lib(project, "AVFoundation");
		}
		add_define(project, "IRON_METAL");
		project->ios.build   = string("%d", version_code());
		project->ios.version = string("%d", version_code());
	}
	add_define(project, "IRON_BGRA");

	// GLSL shaders: glslang (GLSL -> SPIR-V) and SPIRV-Cross (SPIR-V -> MSL), see iron_glsl.c
	add_define(project, "WITH_GLSL");
	add_define(project, "SPIRV_CROSS_C_API_MSL=1");
	add_define(project, "SPIRV_CROSS_C_API_GLSL=1");
	add_include_dir(project, "sources/libs/glslang");
	add_include_dir(project, "sources/libs/spirv_cross");
	add_cfiles(project, "sources/libs/glslang/glslang/MachineIndependent/*.cpp");
	add_cfiles(project, "sources/libs/glslang/glslang/MachineIndependent/preprocessor/*.cpp");
	add_cfiles(project, "sources/libs/glslang/glslang/GenericCodeGen/*.cpp");
	add_cfiles(project, "sources/libs/glslang/glslang/OSDependent/Unix/ossource.cpp");
	add_cfiles(project, "sources/libs/glslang/glslang/CInterface/*.cpp");
	add_cfiles(project, "sources/libs/glslang/glslang/ResourceLimits/*.cpp");
	add_cfiles(project, "sources/libs/glslang/SPIRV/*.cpp");
	add_cfiles(project, "sources/libs/glslang/SPIRV/CInterface/*.cpp");
	add_cfiles(project, "sources/libs/spirv_cross/*.cpp");

	if (flags->with_plugins) {
		add_define(project, "WITH_PLUGINS");
		add_project(project, string("%s/plugins", flags->dirname));
	}

	if (flags->embed) {
		add_define(project, "WITH_EMBED");
		add_define(project, "arm_embed");
	}

	if (flags->with_physics) {
		add_define(project, "WITH_PHYSICS");
	}

	if (flags->with_raytrace) {
		add_define(project, "WITH_RAYTRACE");
		add_assets(project, "assets/raytrace/*", "data/{name}", 0);
		add_assets(project, "shaders/raytrace/*.metal", "data/{name}", 0);
	}

	if (flags->export_version_info) {
		char *dir  = string("%s/build", flags->dirname);
		char *sha  = os_popen("git log --pretty=format:\"%h\" -n 1");
		char *data = string("{ \"sha\": \"%.7s\", \"date\": \"%s\" }", sha, date_string());
		fs_ensuredir(dir);
		fs_writefile(string("%s/version.json", dir), data);
		// Adds version.json to embed.txt list
		add_assets(project, string("%s/version.json", dir), "data/{name}", 0);
	}

	if (flags->with_audio) {
		add_define(project, "IRON_AUDIO");
		add_cfiles(project, "sources/libs/stb_vorbis.c");
	}

	if (flags->with_eval) {
		add_define(project, "WITH_EVAL");
		add_cfiles(project, "sources/libs/minic.c");
		add_cfiles(project, "sources/libs/minic_tests.c");
	}

	if (flags->with_bc7) {
		add_define(project, "WITH_BC7");
		add_cfiles(project, "sources/libs/bc7enc.c");
	}

	if (flags->with_gamepad) {
		add_define(project, "WITH_GAMEPAD");
	}

	if (platform == PLATFORM_MACOS) {
		add_define(project, "WITH_WEBSOCKET");
		add_cfiles(project, "sources/backends/macos_websocket.m");
	}

	if (flags->idle_sleep) {
		add_define(project, "IDLE_SLEEP");
	}

	char *root = flags->dirname;
	if (fs_exists(string("%s/icon.png", root))) {
		project->icon = "icon.png";
		if (platform == PLATFORM_MACOS && fs_exists(string("%s/icon_macos.png", root))) {
			project->icon = "icon_macos.png";
		}
		else if (platform == PLATFORM_IOS && fs_exists(string("%s/icon_ios.png", root))) {
			project->icon = "icon_ios.png";
		}
	}

	if (flags->with_nfd && platform == PLATFORM_MACOS) {
		add_define(project, "WITH_NFD");
		add_cfiles(project, "sources/libs/nfd.c");
		add_cfiles(project, "sources/libs/nfd.m");
	}

	if (flags->with_compress) {
		add_define(project, "WITH_COMPRESS");
		add_cfiles(project, "sources/libs/untar.c");
	}

	if (flags->with_image_write) {
		add_define(project, "WITH_IMAGE_WRITE");
	}

	if (flags->with_video_write) {
		add_define(project, "WITH_VIDEO_WRITE");
	}

	project_flatten(project);
	return project;
}
