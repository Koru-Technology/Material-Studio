#include "../global.h"

// Materials of the library folder (library.c), or with the cloud button the Poly Haven and ambientCG materials
// (library_cloud.c): previews only, tapping one adds it to the project

static char *tab_library_search = "";
static bool  tab_library_cloud  = false;
static i32   tab_library_type   = 0; // Combo position: 0 - all, then the types that have items

static bool tab_library_matches(library_entry_t *e) {
	if (string_equals(tab_library_search, "")) {
		return true;
	}
	char *search = to_lower_case(tab_library_search);
	return string_index_of(to_lower_case(library_entry_rel(e)), search) >= 0;
}

// Search field with the cloud button next to it
static void tab_library_draw_header() {
	ui_row(f32_array_create_from_raw_tmp((f32[]){0.82, 0.18}, 2));
	tab_library_search = string_copy(ui_text_input(&tab_library_search, tr("Search"), UI_ALIGN_LEFT, true, true));
	u32 col = g_theme->BUTTON_COL;
	if (tab_library_cloud) {
		g_theme->BUTTON_COL = g_theme->HIGHLIGHT_COL;
	}
	if (ui_icon_button("", ICON_CLOUD, UI_ALIGN_CENTER)) {
		tab_library_cloud = !tab_library_cloud;
	}
	g_theme->BUTTON_COL = col;
	if (g_ui->is_hovered) {
		ui_tooltip(tab_library_cloud ? tr("Show my materials") : tr("Show the cloud materials"));
	}
}

typedef struct tab_library_grid {
	i32          num;
	f32          imgh;
	i32          text_h;
	i32          row_h;
	f32_array_t *ratios;
} tab_library_grid_t;

static tab_library_grid_t tab_library_grid() {
	tab_library_grid_t g;
	i32                slotw = math_floor(80 * UI_SCALE());
	g.num                    = math_floor(g_ui->_window_w / (float)slotw);
	if (g.num < 1) {
		g.num = 1;
	}
	g.imgh   = 64 * UI_SCALE();
	g.text_h = UI_ELEMENT_H() + UI_ELEMENT_OFFSET();
	g.row_h  = math_floor(g.imgh + g.text_h + 8 * UI_SCALE());
	g.ratios = f32_array_create_from_raw_tmp(NULL, g.num);
	for (i32 i = 0; i < g.num; ++i) {
		g.ratios->buffer[i] = 1 / (float)g.num;
	}
	return g;
}

// Rows out of view only take their space, their previews are not loaded
static bool tab_library_row_hidden(tab_library_grid_t *g) {
	return g_ui->_y + g->row_h < 0 || g_ui->_y > g_ui->_window_h;
}

static ui_state_t tab_library_image(gpu_texture_t *icon, f32 imgh, u32 tint) {
	ui_state_t state;
	g_ui->image_scroll_align = false;
	if (icon != NULL) {
		state = ui_image(icon, tint, imgh);
	}
	else {
		rect_t *r = resource_tile50(resource_get("icons.k"), ICON_SPHERE);
		state     = ui_sub_image(resource_get("icons.k"), 0xff555555, imgh, r->x, r->y, r->w, r->h);
	}
	g_ui->image_scroll_align = true;
	return state;
}

static void tab_library_draw_local() {
	ui_begin_sticky();
	tab_library_draw_header();
	ui_text(library_status(), UI_ALIGN_LEFT, 0x00000000);
	if (!library_has_folder() && ui_icon_button(tr("Choose Folder..."), ICON_FOLDER_OPEN, UI_ALIGN_CENTER)) {
		library_pick_folder();
	}
	ui_end_sticky();
	ui_separator(3, false);

	// Ready items matching the search
	i32  count   = library_entry_count();
	i32 *visible = malloc(sizeof(i32) * (count > 0 ? count : 1));
	i32  shown   = 0;
	for (i32 i = 0; i < count; ++i) {
		library_entry_t *e = library_entry_get(i);
		if (library_entry_ready(e) && !library_entry_broken(e) && tab_library_matches(e)) {
			visible[shown++] = i;
		}
	}

	tab_library_grid_t g = tab_library_grid();
	for (i32 row = 0; row * g.num < shown; ++row) {
		if (tab_library_row_hidden(&g)) {
			g_ui->_y += g.row_h;
			continue;
		}

		ui_row(g.ratios);
		for (i32 j = 0; j < g.num; ++j) {
			i32 k = row * g.num + j;
			if (k >= shown) {
				ui_end_element_of_size(g.imgh);
				continue;
			}
			library_entry_t *e     = library_entry_get(visible[k]);
			ui_state_t       state = tab_library_image(library_entry_icon(e), g.imgh, 0xffffffff);
			if (state == UI_STATE_RELEASED && !g_ui->is_scrolling) {
				library_use(e);
			}
			if (g_ui->is_hovered) {
				ui_tooltip(library_entry_rel(e));
			}
		}

		ui_row(g.ratios);
		for (i32 j = 0; j < g.num; ++j) {
			i32 k = row * g.num + j;
			if (k >= shown) {
				ui_end_element_of_size(g.text_h);
				continue;
			}
			ui_text(library_entry_name(library_entry_get(visible[k])), UI_ALIGN_CENTER, 0x00000000);
		}
		g_ui->_y += 8 * UI_SCALE();
	}
	free(visible);
}

// Download arrow inside a ring that fills up clockwise with the progress
static void tab_library_draw_progress(f32 cx, f32 cy, f32 r, f32 progress) {
	f32 w = 3 * UI_SCALE();
	draw_set_color(0x55ffffff);
	draw_circle(cx, cy, r, 48, w);
	draw_set_color(0xffffffff);
	i32 segments = math_floor(48 * progress);
	for (i32 i = 0; i < segments; ++i) {
		f32 a0 = -math_pi() / 2.0 + i / 48.0 * math_pi() * 2.0;
		f32 a1 = -math_pi() / 2.0 + (i + 1) / 48.0 * math_pi() * 2.0;
		draw_line_aa(cx + math_cos(a0) * r, cy + math_sin(a0) * r, cx + math_cos(a1) * r, cy + math_sin(a1) * r, w);
	}
	f32 h = r * 0.5;
	draw_line_aa(cx, cy - h, cx, cy + h, w);
	draw_line_aa(cx - h * 0.6, cy + h * 0.4, cx, cy + h, w);
	draw_line_aa(cx + h * 0.6, cy + h * 0.4, cx, cy + h, w);
}

// One group of cloud items: a header with the type, then the grid
static void tab_library_draw_cloud_group(tab_library_grid_t *g, i32 *items, i32 shown, char *title) {
	if (title != NULL) {
		if (g_ui->_y + g->text_h < 0 || g_ui->_y > g_ui->_window_h) {
			g_ui->_y += g->text_h;
		}
		else {
			ui_text(title, UI_ALIGN_LEFT, g_theme->SEPARATOR_COL);
		}
	}
	for (i32 row = 0; row * g->num < shown; ++row) {
		if (tab_library_row_hidden(g)) {
			g_ui->_y += g->row_h;
			continue;
		}

		ui_row(g->ratios);
		for (i32 j = 0; j < g->num; ++j) {
			i32 k = row * g->num + j;
			if (k >= shown) {
				ui_end_element_of_size(g->imgh);
				continue;
			}
			cloud_item_t *it    = library_cloud_get(items[k]);
			bool          busy  = library_cloud_item_busy(it);
			f32           x     = g_ui->_x;
			f32           y     = g_ui->_y;
			ui_state_t    state = tab_library_image(library_cloud_item_icon(it), g->imgh, busy ? 0xff666666 : 0xffffffff);
			if (state == UI_STATE_RELEASED && !g_ui->is_scrolling) {
				library_cloud_use(it);
			}
			if (g_ui->is_hovered) {
				ui_tooltip(string("%s (%s)", library_cloud_item_name(it), library_cloud_item_source(it)));
			}
			if (busy) {
				tab_library_draw_progress(x + g->imgh / 2.0, y + g->imgh / 2.0, g->imgh * 0.25, library_cloud_item_progress(it));
			}
			else if (library_cloud_item_downloaded(it)) {
				// Check in a dark circle at the corner
				f32            s     = g->imgh * 0.3;
				f32            cx    = x + g->imgh - s / 2.0;
				f32            cy    = y + s / 2.0;
				gpu_texture_t *icons = resource_get("icons.k");
				rect_t        *r     = resource_tile50(icons, ICON_CHECK);
				draw_set_color(0x33000000);
				draw_filled_circle(cx, cy, s * 0.6, 0);
				draw_set_color(0xff7fd17f);
				draw_scaled_sub_image(icons, r->x, r->y, r->w, r->h, cx - s / 2.0, cy - s / 2.0, s, s);
				draw_set_color(0xffffffff);
			}
		}

		ui_row(g->ratios);
		for (i32 j = 0; j < g->num; ++j) {
			i32 k = row * g->num + j;
			if (k >= shown) {
				ui_end_element_of_size(g->text_h);
				continue;
			}
			ui_text(library_cloud_item_name(library_cloud_get(items[k])), UI_ALIGN_CENTER, 0x00000000);
		}
		g_ui->_y += 8 * UI_SCALE();
	}
}

static void tab_library_draw_cloud() {
	library_cloud_open();

	// Items matching the search, counted per type
	i32   count  = library_cloud_count();
	i32   types  = library_cloud_type_count();
	i32  *items  = malloc(sizeof(i32) * (count > 0 ? count : 1));
	i32  *starts = calloc(types + 1, sizeof(i32));
	i32   shown  = 0;
	char *search = to_lower_case(tab_library_search);
	bool  any    = !string_equals(search, "");
	for (i32 i = 0; i < count; ++i) {
		cloud_item_t *it = library_cloud_get(i);
		if (!any || library_cloud_item_matches(it, search)) {
			items[shown++] = i;
			starts[library_cloud_item_type(it) + 1]++;
		}
	}

	// Type combo: all, then the types with items
	string_array_t *names = any_array_create(0);
	i32             ids[32];
	i32             id_count = 0;
	any_array_push(names, string("%s (%d)", tr("All Types"), shown));
	ids[id_count++] = -1;
	for (i32 t = 0; t < types; ++t) {
		if (starts[t + 1] > 0) {
			any_array_push(names, string("%s (%d)", library_cloud_type_name(t), starts[t + 1]));
			ids[id_count++] = t;
		}
	}
	for (i32 t = 0; t < types; ++t) {
		starts[t + 1] += starts[t];
	}

	ui_begin_sticky();
	tab_library_draw_header();
	if (tab_library_type >= id_count) {
		tab_library_type = 0;
	}
	ui_combo(&tab_library_type, names, tr("Type"), false, UI_ALIGN_LEFT, false);
	// Another type picked: back to the top
	static i32 last_type = 0;
	if (tab_library_type != last_type) {
		last_type                           = tab_library_type;
		g_ui->current_window->scroll_offset = 0;
		g_ui->current_window->redraws       = 2;
	}
	char *status = library_cloud_status();
	if (!string_equals(status, "")) {
		ui_text(status, UI_ALIGN_LEFT, 0x00000000);
	}
	ui_end_sticky();
	ui_separator(3, false);

	// Items are sorted by type, each type is a range of the matching items
	tab_library_grid_t g    = tab_library_grid();
	i32                only = ids[tab_library_type];
	for (i32 t = 0; t < types; ++t) {
		i32 n = starts[t + 1] - starts[t];
		if (n == 0 || (only >= 0 && t != only)) {
			continue;
		}
		tab_library_draw_cloud_group(&g, items + starts[t], n, only >= 0 ? NULL : library_cloud_type_name(t));
	}
	free(items);
	free(starts);
}

void tab_library_draw(i32 *htab) {
	if (!ui_tab(htab, tr("Library"), false, -1, false)) {
		return;
	}
	tab_library_cloud ? tab_library_draw_cloud() : tab_library_draw_local();
}
