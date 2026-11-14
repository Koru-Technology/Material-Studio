#include "../base/tools/amake/amake.h"

project_t *main() {
	flags->name                = "MaterialStudio";
	flags->package             = "technology.koru.materialstudio";
	flags->embed               = has_arg("--embed"); // !has_arg("--debug"); // clang 19
	flags->with_physics        = true;
	flags->with_d3dcompiler    = true;
	flags->with_nfd            = true;
	flags->with_compress       = platform != PLATFORM_ANDROID;
	flags->with_image_write    = true;
	flags->with_video_write    = !has_arg("tcc");
	flags->with_eval           = true;
	flags->with_plugins        = true;
	flags->with_raytrace       = true;
	flags->with_bc7            = true;
	flags->with_audio          = false;
	flags->idle_sleep          = !has_arg("--debug");
	flags->export_version_info = true;
	flags->export_data_list    = platform == PLATFORM_ANDROID; // .apk contents
	bool with_tcc              = false;                        // Compile scripts with libtcc
	bool with_mipmaps          = false;

	if (platform == PLATFORM_WASM) {
		flags->with_nfd         = false;
		flags->with_compress    = false;
		flags->with_plugins     = false;
		flags->export_data_list = true;
	}

	project_t *project = project_create(flags->name);
	add_project(project, "../base");
	add_cfiles(project, "sources/startup.c");
	add_cfiles(project, "sources/minic_api.c");
	add_cfiles(project, "sources/main.c");
	// Material Maker generator, own translation units (their file-local helpers share names), mm_core.c is in main.c
	add_cfiles(project, "sources/material_maker/mm_gen.c");
	add_cfiles(project, "sources/material_maker/mm_graph.c");
	add_cfiles(project, "sources/material_maker/mm_plan.c");
	add_cfiles(project, "sources/material_maker/mm_sdf.c");
	add_cfiles(project, "sources/material_maker/mm_types_extra.c");
	add_cfiles(project, "sources/material_maker/mm_texture.c");
	add_cfiles(project, "sources/material_maker/mm_embed.c");
	add_shaders(project, "shaders/*.shader");
	add_shaders(project, "shaders/*.glsl");
	add_assets(project, "assets/*", "data/{name}", 0);
	add_assets(project, "assets/export_presets/*", "data/export_presets/{name}", 0);
	add_assets(project, "assets/keymap_presets/*", "data/keymap_presets/{name}", 0);
	add_assets(project, "assets/licenses/**", "data/licenses/{name}", 0);
	add_assets(project, "assets/meshes/*", "data/meshes/{name}", ASSET_NOEMBED);
	add_assets(project, "assets/meshes/default/*", "data/meshes/{name}", 0); // Embed default mesh
	add_assets(project, "assets/locale/*", "data/locale/{name}", 0);
	add_assets(project, "assets/material_maker/*", "data/material_maker/{name}", 0); // nodes.pack, library.pack
	// The library's buffer passes precompiled for Metal (tools/material_maker/build_metallib.py, read by
	// metal_msl_pack.h) are not shipped: ~35 MB per platform for a first-use-only saving, iOS caches compiled shaders.
	// To ship them: generate assets/material_maker/msl/msl_<platform>.pack and add it here as data/material_maker/{name},
	// ASSET_NOEMBED.
	if (with_tcc) {
		add_define(project, "WITH_TCC");
		add_cfiles(project, "../base/sources/libs/minic_tcc.c");
		if (!fs_exists("build/tinycc/libtcc.c")) {
			os_popen(string("unzip -qo %s/../base/tools/tcc/src.zip -d %s/build", flags->dirname, flags->dirname));
			fs_writefile("build/tinycc/config.h", "#define TCC_VERSION \"0.9.28rc\"\n#define CONFIG_TCC_PREDEFS 1\n");
			fs_writefile("build/tinycc/tccdefs_.h", "\"\"\n");
		}
		add_include_dir(project, "build/tinycc");
		add_cfiles(project, "build/tinycc/libtcc.c");
	}
	if (!flags->embed) {
		add_assets(project, "../base/assets/extra/*", "data/{name}", 0); // font_cjk.ttc
	}
	if (with_mipmaps) {
		add_define(project, "WITH_MIPMAPS");
	}
	return project;
}
