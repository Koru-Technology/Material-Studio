#include "../global.h"

// Materials saved from the Material Studio, of the library folder (library.c) and Material Maker's library (mm_library.c),
// or with the cloud button the Poly Haven, ambientCG and FreeStylized materials (library_cloud.c): previews only, tapping
// one adds it to the project

static char *tab_library_search = "";
// Cloud view, its type and the scroll position are kept in the config (library_cloud, library_cloud_type, library_scroll)

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
	if (g_config->library_cloud) {
		g_theme->BUTTON_COL = g_theme->HIGHLIGHT_COL;
	}
	if (ui_icon_button("", ICON_CLOUD, UI_ALIGN_CENTER)) {
		g_config->library_cloud             = !g_config->library_cloud;
		g_ui->current_window->scroll_offset = 0;
		g_ui->current_window->redraws       = 2;
		config_save();
	}
	g_theme->BUTTON_COL = col;
	if (g_ui->is_hovered) {
		ui_tooltip(g_config->library_cloud ? tr("Show my materials") : tr("Show the cloud materials"));
	}
}

typedef struct tab_library_grid {
	i32          num;
	f32          imgh;
	i32          text_h; // Two lines of name
	i32          head_h; // A section header
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
	g.text_h = UI_ELEMENT_H() + draw_font_height(g_ui->ops->font, g_ui->font_size) + UI_ELEMENT_OFFSET();
	g.head_h = UI_ELEMENT_H() + UI_ELEMENT_OFFSET();
	g.row_h  = math_floor(g.imgh + g.text_h + 8 * UI_SCALE());
	g.ratios = f32_array_create_from_raw_tmp(NULL, g.num);
	for (i32 i = 0; i < g.num; ++i) {
		g.ratios->buffer[i] = 1 / (float)g.num;
	}
	return g;
}

// The name under a preview, centered, wrapped at a space onto a second line when it is too wide (that line cut with
// ".." when still too wide, the full name in the tooltip). Always two lines high, so every item of a row lines up.
static void tab_library_label(char *name, tab_library_grid_t *g) {
	static char first[256];
	f32         max_w = g_ui->_w - 6.0 * UI_SCALE();
	char       *rest  = NULL;
	draw_set_font(g_ui->ops->font, g_ui->font_size);
	snprintf(first, sizeof(first), "%s", name);
	if (draw_string_width(g_ui->ops->font, g_ui->font_size, first) > max_w) {
		i32 cut = -1; // The last space the text before fits
		for (i32 i = 0; first[i] != 0; ++i) {
			if (first[i] == ' ') {
				first[i] = 0;
				bool fits = draw_string_width(g_ui->ops->font, g_ui->font_size, first) <= max_w;
				first[i] = ' ';
				if (!fits) {
					break;
				}
				cut = i;
			}
		}
		if (cut > 0) {
			first[cut] = 0;
			rest       = &first[cut + 1];
			while (*rest == ' ') {
				rest++;
			}
		}
	}
	ui_get_hover(g->text_h); // The tooltip of a cut name is this item's
	draw_set_color(g_theme->TEXT_COL);
	ui_draw_string(first, -1, 0, UI_ALIGN_CENTER, true);
	if (rest != NULL && rest[0] != 0) {
		ui_draw_string(rest, -1, draw_font_height(g_ui->ops->font, g_ui->font_size), UI_ALIGN_CENTER, true);
	}
	ui_end_element_of_size(g->text_h);
}

// Rows out of view only take their space, their previews are not loaded
static bool tab_library_row_hidden(tab_library_grid_t *g) {
	return g_ui->_y + g->row_h < 0 || g_ui->_y > g_ui->_window_h;
}

// Centered in its cell: moved right by half the space left over, and the cell narrowed by as much so the next one starts
// where it did
static ui_state_t tab_library_image(gpu_texture_t *icon, f32 imgh, u32 tint) {
	ui_state_t state;
	f32        spare = g_ui->_w - imgh;
	if (spare > 0) {
		g_ui->_x += spare / 2.0;
		g_ui->_w -= spare / 2.0;
	}
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

// Groups of the type menus, of Material Maker's library and of the cloud: all, all of a style, or one type of a style.
// The stylized types come first, then the realistic ones.
#define TAB_LIBRARY_ALL           -1
#define TAB_LIBRARY_ALL_STYLIZED  -2
#define TAB_LIBRARY_ALL_REALISTIC -3

static char *tab_library_style_name(bool stylized) {
	return stylized ? tr("Stylized") : tr("Realistic");
}

// Group of Material Maker's library picked in the type menu: TAB_LIBRARY_ALL..., or a section of a style, the stylized
// sections first. Its counts are kept for the menu, which draws outside of the tab.
#define TAB_LIBRARY_MM_SECTIONS 32
static i32  tab_library_mm_group = TAB_LIBRARY_ALL;
static i32  tab_library_mm_counts[TAB_LIBRARY_MM_SECTIONS * 2];
static i32  tab_library_mm_styled = 0; // Stylized materials matching the search
static i32  tab_library_mm_total  = 0;
static bool tab_library_mm_picked = false;

static i32 tab_library_mm_sections() {
	i32 n = mm_library_section_count();
	return n < TAB_LIBRARY_MM_SECTIONS ? n : TAB_LIBRARY_MM_SECTIONS;
}

static i32 tab_library_mm_group_of(i32 i) {
	return (mm_library_stylized(i) ? 0 : tab_library_mm_sections()) + mm_library_section(i);
}

static void tab_library_mm_count() {
	char *search          = to_lower_case(tab_library_search);
	i32   sections        = tab_library_mm_sections();
	tab_library_mm_total  = 0;
	tab_library_mm_styled = 0;
	for (i32 g = 0; g < TAB_LIBRARY_MM_SECTIONS * 2; ++g) {
		tab_library_mm_counts[g] = 0;
	}
	for (i32 i = 0; i < mm_library_count(); ++i) {
		i32 s = mm_library_section(i);
		if (s >= 0 && s < sections && mm_library_matches(i, search)) {
			tab_library_mm_counts[tab_library_mm_group_of(i)]++;
			tab_library_mm_styled += mm_library_stylized(i) ? 1 : 0;
			tab_library_mm_total++;
		}
	}
	// The picked group, all when it has nothing to show now
	i32  g     = tab_library_mm_group;
	bool empty = g >= 0 ? (g >= sections * 2 || tab_library_mm_counts[g] == 0)
	                    : (g == TAB_LIBRARY_ALL_STYLIZED && tab_library_mm_styled == 0) ||
	                          (g == TAB_LIBRARY_ALL_REALISTIC && tab_library_mm_total - tab_library_mm_styled == 0);
	if (empty) {
		tab_library_mm_group = TAB_LIBRARY_ALL;
	}
}

static char *tab_library_mm_group_name(i32 g) {
	i32 sections = tab_library_mm_sections();
	if (g >= 0) {
		return string("%s %s (%d)", tab_library_style_name(g < sections), mm_library_section_name(g % sections), tab_library_mm_counts[g]);
	}
	if (g == TAB_LIBRARY_ALL) {
		return string("%s (%d)", tr("All Types"), tab_library_mm_total);
	}
	bool stylized = g == TAB_LIBRARY_ALL_STYLIZED;
	return string("%s (%d)", tab_library_style_name(stylized), stylized ? tab_library_mm_styled : tab_library_mm_total - tab_library_mm_styled);
}

static void tab_library_mm_pick(i32 g, char *name) {
	bool current = g == tab_library_mm_group;
	if (ui_menu_button(name, "", current ? ICON_CHECK : ICON_NONE) && !current) {
		tab_library_mm_group  = g;
		tab_library_mm_picked = true;
		ui_dock_redraw(TAB_AREA_SIDEBAR1);
	}
}

// All, then a heading per style with all of it and its sections
static void tab_library_mm_menu() {
	i32 sections = tab_library_mm_sections();
	tab_library_mm_pick(TAB_LIBRARY_ALL, tab_library_mm_group_name(TAB_LIBRARY_ALL));
	for (i32 st = 0; st < 2; ++st) {
		bool stylized = st == 0;
		i32  n        = stylized ? tab_library_mm_styled : tab_library_mm_total - tab_library_mm_styled;
		if (n == 0) {
			continue;
		}
		ui_menu_separator();
		ui_menu_label(tab_library_style_name(stylized), NULL);
		tab_library_mm_pick(stylized ? TAB_LIBRARY_ALL_STYLIZED : TAB_LIBRARY_ALL_REALISTIC, string("%s (%d)", tr("All"), n));
		for (i32 s = 0; s < sections; ++s) {
			i32 g = st * sections + s;
			if (tab_library_mm_counts[g] > 0) {
				tab_library_mm_pick(g, string("%s (%d)", mm_library_section_name(s), tab_library_mm_counts[g]));
			}
		}
	}
}

// Looks like a combo, opens the type menu under it
static void tab_library_mm_type_button() {
	char *picked = tab_library_mm_group_name(tab_library_mm_group);
	f32   by     = g_ui->_y;
	if (ui_button(picked, UI_ALIGN_LEFT, "")) {
		i32 menu_w = math_floor(base_default_element_w * UI_SCALE() * 2.3);
		ui_menu_draw(&tab_library_mm_menu, iron_window_width() - menu_w, math_floor(g_ui->_window_y + g_ui->_y));
	}
	f32 ax = g_ui->_x + g_ui->_w - g_ui->arrow_offset_x - 8;
	f32 ay = by + g_ui->arrow_offset_y + 3;
	draw_set_color(g_theme->HOVER_COL);
	draw_filled_triangle(ax, ay, ax + UI_ARROW_SIZE(), ay, ax + UI_ARROW_SIZE() / 2.0, ay + UI_ARROW_SIZE() / 2.0);
}

// Material Maker's library (nodes_material/mm_library.c), one group per style and section, or the picked ones. Only the
// rows in view load their thumbnails.
static void tab_library_draw_mm(tab_library_grid_t *g) {
	char *search   = to_lower_case(tab_library_search);
	i32   total    = mm_library_count();
	i32   sections = tab_library_mm_sections();
	i32   only     = tab_library_mm_group;
	i32  *items    = malloc(sizeof(i32) * (total > 0 ? total : 1));
	for (i32 grp = 0; grp < sections * 2; ++grp) {
		bool stylized = grp < sections;
		bool show     = only == TAB_LIBRARY_ALL || only == grp || (only == TAB_LIBRARY_ALL_STYLIZED && stylized) ||
		            (only == TAB_LIBRARY_ALL_REALISTIC && !stylized);
		if (!show) {
			continue;
		}
		i32 shown = 0;
		for (i32 i = 0; i < total; ++i) {
			i32 s = mm_library_section(i);
			if (s >= 0 && s < sections && tab_library_mm_group_of(i) == grp && mm_library_matches(i, search)) {
				items[shown++] = i;
			}
		}
		if (shown == 0) {
			continue;
		}
		if (g_ui->_y + g->head_h < 0 || g_ui->_y > g_ui->_window_h) {
			g_ui->_y += g->head_h;
		}
		else {
			char *section = mm_library_section_name(grp % sections);
			char *header  = only == TAB_LIBRARY_ALL ? string("%s %s (%d)", tab_library_style_name(stylized), section, shown) : string("%s (%d)", section, shown);
			ui_text(header, UI_ALIGN_LEFT, g_theme->SEPARATOR_COL);
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
				ui_state_t state = tab_library_image(mm_library_icon(items[k]), g->imgh, 0xffffffff);
				if (state == UI_STATE_RELEASED && !g_ui->is_scrolling) {
					mm_library_use(items[k]);
				}
				if (g_ui->is_hovered) {
					ui_tooltip(mm_library_credit(items[k]));
				}
			}
			ui_row(g->ratios);
			for (i32 j = 0; j < g->num; ++j) {
				i32 k = row * g->num + j;
				if (k >= shown) {
					ui_end_element_of_size(g->text_h);
					continue;
				}
				tab_library_label(mm_library_name(items[k]), g);
			}
			g_ui->_y += 8 * UI_SCALE();
		}
	}
	free(items);
}

static i32 tab_library_saved_i = -1;

static void tab_library_saved_menu() {
	if (ui_menu_button(tr("Delete"), "", ICON_DELETE)) {
		saved_materials_delete(tab_library_saved_i);
		ui_dock_redraw(TAB_AREA_SIDEBAR1);
	}
}

// Materials saved from the Material Studio (saved_materials.c), long press or right click to delete
static void tab_library_draw_saved(tab_library_grid_t *g) {
	char *search = to_lower_case(tab_library_search);
	i32   total  = saved_materials_count();
	i32  *items  = malloc(sizeof(i32) * (total > 0 ? total : 1));
	i32   shown  = 0;
	for (i32 i = 0; i < total; ++i) {
		if (string_index_of(to_lower_case(saved_materials_name(i)), search) >= 0) {
			items[shown++] = i;
		}
	}
	if (shown > 0) {
		ui_text(tr("Saved"), UI_ALIGN_LEFT, g_theme->SEPARATOR_COL);
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
			ui_state_t state = tab_library_image(saved_materials_icon(items[k]), g->imgh, 0xffffffff);
			if (state == UI_STATE_RELEASED && !g_ui->is_scrolling) {
				saved_materials_use(items[k]);
			}
			if (g_ui->is_hovered) {
				ui_tooltip(saved_materials_name(items[k]));
				if (g_ui->input_released_r) {
					tab_library_saved_i = items[k];
					ui_menu_draw(&tab_library_saved_menu, -1, -1);
				}
			}
		}
		ui_row(g->ratios);
		for (i32 j = 0; j < g->num; ++j) {
			i32 k = row * g->num + j;
			if (k >= shown) {
				ui_end_element_of_size(g->text_h);
				continue;
			}
			tab_library_label(saved_materials_name(items[k]), g);
		}
		g_ui->_y += 8 * UI_SCALE();
	}
	free(items);
}

static void tab_library_draw_local() {
	tab_library_mm_count();
	ui_begin_sticky();
	tab_library_draw_header();
	if (tab_library_mm_picked) { // Another type picked: back to the top
		tab_library_mm_picked               = false;
		g_ui->current_window->scroll_offset = 0;
		g_ui->current_window->redraws       = 2;
	}
	tab_library_mm_type_button();
	ui_text(library_status(), UI_ALIGN_LEFT, 0x00000000);
	if (!library_has_folder() && ui_icon_button(tr("Choose Folder..."), ICON_FOLDER_OPEN, UI_ALIGN_CENTER)) {
		library_pick_folder();
	}
	ui_end_sticky();
	ui_separator(3, false);

	tab_library_grid_t g = tab_library_grid();
	if (tab_library_mm_group != TAB_LIBRARY_ALL) { // A group of Material Maker's library picked: only that
		tab_library_draw_mm(&g);
		return;
	}
	tab_library_draw_saved(&g);

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

	if (shown > 0) {
		ui_text(tr("My Materials"), UI_ALIGN_LEFT, g_theme->SEPARATOR_COL);
	}
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
			tab_library_label(library_entry_name(library_entry_get(visible[k])), &g);
		}
		g_ui->_y += 8 * UI_SCALE();
	}
	free(visible);

	// Material Maker's library, after the user's own materials
	tab_library_draw_mm(&g);
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
		if (g_ui->_y + g->head_h < 0 || g_ui->_y > g_ui->_window_h) {
			g_ui->_y += g->head_h;
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
			f32           x     = g_ui->_x + (g_ui->_w > g->imgh ? (g_ui->_w - g->imgh) / 2.0 : 0); // Where the centered preview is
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
			tab_library_label(library_cloud_item_name(library_cloud_get(items[k])), g);
		}
		g_ui->_y += 8 * UI_SCALE();
	}
}

#define TAB_LIBRARY_HEADING -100
#define TAB_LIBRARY_MENU_MAX 64

static char tab_library_menu_names[TAB_LIBRARY_MENU_MAX][128];
static i32  tab_library_menu_ids[TAB_LIBRARY_MENU_MAX];
static i32  tab_library_menu_count  = 0;
static bool tab_library_type_picked = false;

static void tab_library_menu_add(i32 id, char *name) {
	if (tab_library_menu_count < TAB_LIBRARY_MENU_MAX) {
		snprintf(tab_library_menu_names[tab_library_menu_count], 128, "%s", name);
		tab_library_menu_ids[tab_library_menu_count++] = id;
	}
}

static void tab_library_type_menu() {
	for (i32 k = 0; k < tab_library_menu_count; ++k) {
		i32 id = tab_library_menu_ids[k];
		if (id == TAB_LIBRARY_HEADING) {
			ui_menu_separator();
			ui_menu_label(tab_library_menu_names[k], NULL);
			continue;
		}
		bool current = id == g_config->library_cloud_type;
		if (ui_menu_button(tab_library_menu_names[k], "", current ? ICON_CHECK : ICON_NONE) && !current) {
			g_config->library_cloud_type = id;
			tab_library_type_picked      = true;
			config_save();
			ui_dock_redraw(TAB_AREA_SIDEBAR1);
		}
	}
}

static void tab_library_draw_cloud() {
	library_cloud_open();

	// Items matching the search, ordered by group (style, then type) and counted per group
	i32   count   = library_cloud_count();
	i32   types   = library_cloud_type_count();
	i32   groups  = types * 2;
	i32  *matches = malloc(sizeof(i32) * (count > 0 ? count : 1));
	i32  *items   = malloc(sizeof(i32) * (count > 0 ? count : 1));
	i32  *starts  = calloc(groups + 1, sizeof(i32));
	i32  *fill    = calloc(groups, sizeof(i32));
	i32   shown   = 0;
	i32   styled  = 0;
	char *search  = to_lower_case(tab_library_search);
	bool  any     = !string_equals(search, "");
	for (i32 i = 0; i < count; ++i) {
		cloud_item_t *it = library_cloud_get(i);
		if (!any || library_cloud_item_matches(it, search)) {
			bool stylized     = library_cloud_item_stylized(it);
			matches[shown++]  = i;
			styled           += stylized ? 1 : 0;
			starts[(stylized ? 0 : types) + library_cloud_item_type(it) + 1]++;
		}
	}
	for (i32 g = 0; g < groups; ++g) {
		starts[g + 1] += starts[g];
	}
	for (i32 k = 0; k < shown; ++k) { // Stable, the items keep their name order
		cloud_item_t *it        = library_cloud_get(matches[k]);
		i32           g         = (library_cloud_item_stylized(it) ? 0 : types) + library_cloud_item_type(it);
		items[starts[g] + fill[g]++] = matches[k];
	}
	free(matches);
	free(fill);

	// Type menu: all, then a heading per style with its types. Kept for the menu, which draws outside of the tab
	tab_library_menu_count = 0;
	tab_library_menu_add(TAB_LIBRARY_ALL, string("%s (%d)", tr("All Types"), shown));
	for (i32 s = 0; s < 2; ++s) {
		bool stylized = s == 0;
		i32  n        = stylized ? styled : shown - styled;
		if (n == 0) {
			continue;
		}
		tab_library_menu_add(TAB_LIBRARY_HEADING, tab_library_style_name(stylized));
		tab_library_menu_add(stylized ? TAB_LIBRARY_ALL_STYLIZED : TAB_LIBRARY_ALL_REALISTIC, string("%s (%d)", tr("All"), n));
		for (i32 t = 0; t < types; ++t) {
			i32 g = s * types + t;
			if (starts[g + 1] > starts[g]) {
				tab_library_menu_add(g, string("%s (%d)", library_cloud_type_name(t), starts[g + 1] - starts[g]));
			}
		}
	}
	// The remembered group, all when it has no items now
	i32 only = TAB_LIBRARY_ALL;
	for (i32 k = 0; k < tab_library_menu_count; ++k) {
		if (tab_library_menu_ids[k] == g_config->library_cloud_type && tab_library_menu_ids[k] != TAB_LIBRARY_HEADING) {
			only = g_config->library_cloud_type;
		}
	}
	char *picked_name;
	if (only >= 0) {
		picked_name = string("%s %s (%d)", tab_library_style_name(only < types), library_cloud_type_name(only % types), starts[only + 1] - starts[only]);
	}
	else if (only == TAB_LIBRARY_ALL) {
		picked_name = string("%s (%d)", tr("All Types"), shown);
	}
	else {
		bool stylized = only == TAB_LIBRARY_ALL_STYLIZED;
		picked_name   = string("%s (%d)", tab_library_style_name(stylized), stylized ? styled : shown - styled);
	}

	ui_begin_sticky();
	tab_library_draw_header();
	if (tab_library_type_picked) { // Another group picked: back to the top
		tab_library_type_picked             = false;
		g_ui->current_window->scroll_offset = 0;
		g_ui->current_window->redraws       = 2;
	}
	// Looks like a combo, opens the menu under it
	f32 by = g_ui->_y;
	if (ui_button(picked_name, UI_ALIGN_LEFT, "")) {
		i32 menu_w = math_floor(base_default_element_w * UI_SCALE() * 2.3);
		ui_menu_draw(&tab_library_type_menu, iron_window_width() - menu_w, math_floor(g_ui->_window_y + g_ui->_y));
	}
	f32 ax = g_ui->_x + g_ui->_w - g_ui->arrow_offset_x - 8;
	f32 ay = by + g_ui->arrow_offset_y + 3;
	draw_set_color(g_theme->HOVER_COL);
	draw_filled_triangle(ax, ay, ax + UI_ARROW_SIZE(), ay, ax + UI_ARROW_SIZE() / 2.0, ay + UI_ARROW_SIZE() / 2.0);
	char *status = library_cloud_status();
	if (!string_equals(status, "")) {
		ui_text(status, UI_ALIGN_LEFT, 0x00000000);
	}
	ui_end_sticky();
	ui_separator(3, false);

	// Each group is a range of the ordered items
	tab_library_grid_t g = tab_library_grid();
	for (i32 k = 0; k < groups; ++k) {
		i32  n        = starts[k + 1] - starts[k];
		bool stylized = k < types;
		bool show     = only == TAB_LIBRARY_ALL || only == k || (only == TAB_LIBRARY_ALL_STYLIZED && stylized) ||
		            (only == TAB_LIBRARY_ALL_REALISTIC && !stylized);
		if (n == 0 || !show) {
			continue;
		}
		char *header = NULL;
		if (only == TAB_LIBRARY_ALL) {
			header = string("%s %s", tab_library_style_name(stylized), library_cloud_type_name(k % types));
		}
		else if (only < 0) {
			header = library_cloud_type_name(k % types);
		}
		tab_library_draw_cloud_group(&g, items + starts[k], n, header);
	}
	free(items);
	free(starts);
}

void tab_library_draw(i32 *htab) {
	if (!ui_tab(htab, tr("Library"), false, -1, false)) {
		return;
	}
	// Scroll position of the last session, applied while the content shows up
	static i32 restore = 2;
	if (restore > 0) {
		restore--;
		g_ui->current_window->scroll_offset = g_config->library_scroll;
		g_ui->current_window->redraws       = 2;
	}
	else {
		g_config->library_scroll = g_ui->current_window->scroll_offset;
	}
	g_config->library_cloud ? tab_library_draw_cloud() : tab_library_draw_local();
}
