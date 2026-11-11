
#include "../global.h"

void tab_plugins_draw(i32 *htab) {
	if (ui_tab(htab, tr("Plugins"), false, -1, false)) {

		// Draw plugins
		string_array_t *keys = map_keys(g_plugins);
		for (i32 i = 0; i < keys->length; ++i) {
			plugin_t *p = any_map_get(g_plugins, keys->buffer[i]);
			if (p->on_ui != NULL) {
				minic_ctx_call_fn(p->ctx, p->on_ui, NULL, 0);
			}
		}
		array_free(keys);
		free(keys);
	}
}
