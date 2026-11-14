#include "global.h"

// Thumbnails of Material Maker's library for the Library tab: imports every material of library.pack, renders its preview
// sphere as the Materials tab renders material icons and writes thumbs.pack (nodes_material/mm_library.c) to the save
// folder, with a report of the materials that did not import, failed to compile or look flat. Copy it to
// paint/assets/material_maker/thumbs.pack. Takes a few minutes:
//   MaterialStudio --script paint/tools/material_maker/mm_thumbnails.c

int   step = 0;
char *out  = NULL;
int   last = -1;

void next() {
	if (step == 0) {
		out = string("%s/thumbs.pack", script_save_path());
		printf("[thumbs] rendering %d library materials into %s", mm_import_library_count(), out);
		mm_thumbs_generate(out, 0, -1);
		step = 1;
	}
	else if (!mm_thumbs_finished()) {
		int p = mm_thumbs_progress();
		if (p / 50 != last / 50) {
			printf("[thumbs] %d", p);
		}
		last = p;
	}
	else {
		printf("[thumbs] report\n%s", mm_thumbs_report());
		printf("[thumbs] %d rendered, %d did not import, %d look flat, %d with shader failures", mm_thumbs_count(0), mm_thumbs_count(1), mm_thumbs_count(2),
		       mm_thumbs_count(3));
		printf("[thumbs] done");
		script_quit();
		return;
	}
	script_notify_on_next_frame(next);
}

void main() {
	script_project_new();
	script_notify_on_next_frame(next);
}
