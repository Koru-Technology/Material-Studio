
#include "../global.h"

any_map_t *box_projects_icon_map = NULL;
char      *_box_projects_path;
char      *_box_projects_icon_path;
i32        _box_projects_i;
char      *_box_projects_load_path;
i32        _box_projects_load_frames;

void box_projects_tab_menu_on_next_frame(void *_) {
	iron_delete_file(_box_projects_path);
	iron_delete_file(_box_projects_icon_path);
	char *data_path = substring(_box_projects_path, 0, string_length(_box_projects_path) - 4);
	iron_delete_file(data_path);
	string_array_t *recent_projects = g_config->recent_projects;
	array_splice(recent_projects, _box_projects_i, 1);
}

void box_projects_tab_menu() {
	// if (ui_menu_button(tr("Duplicate"), "ctrl+d", icon_t.DUPLICATE)) {}
	if (ui_menu_button(tr("Delete"), "delete", ICON_DELETE)) {
		sys_notify_on_next_frame(&box_projects_tab_menu_on_next_frame, NULL);
	}
}

// Full path of a recent project, iOS stores only the file name inside the documents folder
char *box_projects_recent_path(i32 i) {
	char *path = g_config->recent_projects->buffer[i];
#ifdef IRON_IOS
	char *document_directory = iron_save_dialog("", "");
	document_directory       = string_copy(substring(document_directory, 0, string_length(document_directory) - 8)); // Strip /"untitled"
	path                     = string("%s%s", document_directory, path);
#endif
	return path;
}

static bool _box_projects_load_cancelled = false;

void box_projects_load_on_next_frame(void *_) {
	if (_box_projects_load_cancelled) {
		_box_projects_load_cancelled = false;
		base_startup_cover_done();
		return;
	}
	if (--_box_projects_load_frames > 0) {
		sys_notify_on_next_frame(&box_projects_load_on_next_frame, NULL);
		return;
	}
	import_arm_run_project(_box_projects_load_path);
	base_startup_cover_done();
}

// A project load that was asked for and has not started yet is dropped (a shared file opens instead)
void box_projects_cancel_load() {
	if (_box_projects_load_frames > 0) {
		_box_projects_load_cancelled = true;
	}
}

void box_projects_load(char *path) {
	_box_projects_load_cancelled = false;
	ui_box_hide();
	_box_projects_load_path   = string_copy(path);
	_box_projects_load_frames = GPU_FRAMES_IN_FLIGHT + 1;
	sys_notify_on_next_frame(&box_projects_load_on_next_frame, NULL);
}

void box_projects_tab_on_next_frame(char *path) {
	box_projects_load(path);
}

#ifdef IRON_IOS // The Mac lists the recent projects instead (box_projects_recent_tab)
static i32 box_projects_no_icon; // Map value of a project without an icon file, so the file is not looked up every frame

// Clears the alpha outside the rounded corners of radius r (in pixels), antialiased
static void box_projects_round_corners(iron_image_pixels_t *p, f32 r) {
	if (p->format != GPU_TEXTURE_FORMAT_RGBA32) {
		return;
	}
	i32 w = p->width;
	i32 h = p->height;
	i32 n = math_ceil(r);
	for (i32 y = 0; y < n && y < h; ++y) {
		for (i32 x = 0; x < n && x < w; ++x) {
			f32 dx = r - (x + 0.5);
			f32 dy = r - (y + 0.5);
			if (dx <= 0 || dy <= 0) {
				continue;
			}
			f32 a       = math_min(math_max(r - math_sqrt(dx * dx + dy * dy) + 0.5, 0.0), 1.0);
			i32 xs[2]   = {x, w - 1 - x};
			i32 ys[2]   = {y, h - 1 - y};
			for (i32 k = 0; k < 4; ++k) {
				u8 *alpha = &p->data[(ys[k / 2] * w + xs[k % 2]) * 4 + 3];
				*alpha    = math_floor(*alpha * a + 0.5);
			}
		}
	}
}

// Preview of a project with its corners rounded, NULL when it has no icon file
static gpu_texture_t *box_projects_icon(char *icon_path, f32 radius_ratio) {
	if (box_projects_icon_map == NULL) {
		box_projects_icon_map = any_map_create();
	}
	gpu_texture_t *icon = any_map_get(box_projects_icon_map, icon_path);
	if (icon == NULL) {
		icon           = (gpu_texture_t *)&box_projects_no_icon;
		buffer_t *blob = iron_load_blob(data_resolve_path(icon_path));
		if (blob != NULL) {
			iron_image_pixels_t pixels;
			if (iron_decode_image(blob, icon_path, &pixels)) {
				box_projects_round_corners(&pixels, pixels.width * radius_ratio);
				icon = gpu_create_texture_from_pixels(&pixels);
			}
			iron_delete_blob(blob);
		}
		any_map_set(box_projects_icon_map, string_copy(icon_path), icon); // The map holds the key
	}
	return icon == (gpu_texture_t *)&box_projects_no_icon ? NULL : icon;
}

static void box_projects_new() {
	project_new(true);
	ui_box_hide();
	// Pick unique name
	i32   i     = 0;
	i32   j     = 0;
	char *title = string("%s%s", tr("untitled"), i32_to_string(i));
	while (j < g_config->recent_projects->length) {
		char *base = g_config->recent_projects->buffer[j];
		base       = string_copy(substring(base, string_last_index_of(base, PATH_SEP) + 1, string_last_index_of(base, ".")));
		j++;
		if (string_equals(title, base)) {
			i++;
			title = string("%s%s", tr("untitled"), i32_to_string(i));
			j     = 0;
		}
	}
	sys_title_set(title);
}

// Title on the left, round "+" button for a new project on the right
static void box_projects_draw_header(f32 pad) {
	f32 s      = UI_SCALE();
	f32 x0     = g_ui->_x;
	f32 y0     = g_ui->_y;
	f32 w0     = g_ui->_w;
	f32 head_h = math_floor(64 * s);

	i32 title_size = math_floor(g_ui->font_size * 1.8);
	draw_set_font(g_font, title_size);
	draw_set_color(g_theme->TEXT_COL);
	draw_string(tr("Material Studio"), x0 + pad, y0 + (head_h - draw_font_height(g_font, title_size)) / 2.0);
	draw_set_font(g_font, g_ui->font_size);

	f32 bs   = math_floor(44 * s);
	g_ui->_x = x0 + w0 - pad - bs;
	g_ui->_y = y0 + (head_h - bs) / 2.0;
	g_ui->_w = bs;
	bool hover    = ui_get_hover(bs);
	bool pushed   = ui_get_pushed(bs);
	bool released = ui_get_released(bs);
	draw_set_color(pushed ? g_theme->PRESSED_COL : hover ? g_theme->HOVER_COL : g_theme->BUTTON_COL);
	ui_base_fill_round(g_ui->_x, g_ui->_y, bs, bs, bs / 2.0);
	gpu_texture_t *icons = resource_get("icons.k");
	rect_t        *rect  = resource_tile50(icons, ICON_PLUS);
	f32            is    = math_floor(28 * s);
	draw_set_color(g_theme->LABEL_COL);
	draw_scaled_sub_image(icons, rect->x, rect->y, rect->w, rect->h, g_ui->_x + (bs - is) / 2.0, g_ui->_y + (bs - is) / 2.0, is, is);
	draw_set_color(0xffffffff);
	if (hover) {
		ui_tooltip(tr("New Project"));
	}
	if (released) {
		box_projects_new();
	}

	g_ui->_x = x0;
	g_ui->_y = y0 + head_h;
	g_ui->_w = w0;
}

// Recent projects: previews of the same size run left to right and wrap to the next row
static void box_projects_draw() {
	f32 s   = UI_SCALE();
	f32 pad = math_floor(24 * s);

	ui_begin_sticky();
	box_projects_draw_header(pad);
	ui_end_sticky();

	f32 tile    = math_floor(160 * s); // Smallest size, the previews grow to fill each row
	f32 gap     = math_floor(24 * s);
	f32 label_h = UI_ELEMENT_H() + math_floor(4 * s);
	f32 radius  = math_floor(12 * s);
	f32 x0      = g_ui->_x;
	f32 y0      = g_ui->_y + math_floor(8 * s);
	f32 w0      = g_ui->_w;
	i32 num     = math_floor((w0 - pad * 2 + gap) / (tile + gap));
	if (num < 1) {
		num = 1;
	}
	tile = math_floor((w0 - pad * 2 - gap * (num - 1)) / num);

	string_array_t *recent_projects = g_config->recent_projects;
	for (i32 i = 0; i < recent_projects->length; ++i) {
		f32 x = x0 + pad + (i % num) * (tile + gap);
		f32 y = y0 + (i / num) * (tile + label_h + gap);
		g_ui->_x = x;
		g_ui->_y = y;
		g_ui->_w = tile;
		if (!ui_is_visible(tile + label_h)) {
			continue;
		}

		char          *path      = box_projects_recent_path(i);
		char          *icon_path = string_tmp("%s_icon.png", substring(path, 0, string_length(path) - 4));
		gpu_texture_t *icon      = box_projects_icon(icon_path, radius / tile);

		bool pushed   = ui_get_pushed(tile);
		bool released = ui_get_released(tile);
		bool hover    = ui_get_hover(tile);

		for (i32 k = 0; k < 3; ++k) { // Deeper than one pass of the menu shadow
			ui_draw_shadow(x, y, tile, tile);
		}
		draw_set_color(0xff5a5a5a); // The grey behind the model in the previews
		ui_base_fill_round(x, y, tile, tile, radius);
		if (icon != NULL) {
			draw_set_color(0xffffffff);
			draw_scaled_image(icon, x, y, tile, tile);
		}
		else { // Saved without a preview
			gpu_texture_t *icons = resource_get("icons.k");
			rect_t        *rect  = resource_tile50(icons, ICON_CUBE);
			f32            is    = math_floor(tile * 0.35);
			draw_set_color(0xff8a8a8a);
			draw_scaled_sub_image(icons, rect->x, rect->y, rect->w, rect->h, x + (tile - is) / 2.0, y + (tile - is) / 2.0, is, is);
		}
		if (pushed) {
			draw_set_color(0x44000000);
			ui_base_fill_round(x, y, tile, tile, radius);
		}
		draw_set_color(0xffffffff);

		if (released) {
			console_toast(tr("Opening project"));
			sys_notify_on_next_frame(&box_projects_tab_on_next_frame, path);
		}
		if (hover && g_ui->input_released_r) {
			_box_projects_path      = string_copy(path);
			_box_projects_icon_path = string_copy(icon_path);
			_box_projects_i         = i;
			ui_menu_draw(&box_projects_tab_menu, -1, -1);
		}

		char *name = substring(path, string_last_index_of(path, PATH_SEP) + 1, string_last_index_of(path, "."));
		g_ui->_y   = y + tile + math_floor(4 * s);
		ui_text(name, UI_ALIGN_CENTER, 0x00000000);
		if (g_ui->is_hovered) {
			ui_tooltip(name);
		}
	}

	i32 rows = math_ceil(recent_projects->length / (float)num);
	g_ui->_x = x0;
	g_ui->_w = w0;
	g_ui->_y = y0 + rows * (tile + label_h + gap) + pad;
}
#endif

void box_projects_align_to_fullscreen() {
	ui_box_modalw       = math_floor(iron_window_width() / (float)UI_SCALE());
	ui_box_modalh       = math_floor(iron_window_height() / (float)UI_SCALE());
	i32 appw            = iron_window_width();
	i32 apph            = iron_window_height();
	i32 mw              = appw;
	i32 mh              = apph;
	ui_box_hwnd->drag_x = math_floor(-appw / 2.0 + mw / 2.0);
	ui_box_hwnd->drag_y = math_floor(-apph / 2.0 + mh / 2.0);
}

void box_projects_show_box() {
#if defined(IRON_IOS)
	box_projects_align_to_fullscreen();
	box_projects_draw(); // Projects only, no tabs
#else
	box_projects_recent_tab();
#endif
}

void box_projects_show() {
	if (box_projects_icon_map != NULL) {
		string_array_t *keys = map_keys(box_projects_icon_map);
		for (i32 i = 0; i < keys->length; ++i) {
			char          *handle = keys->buffer[i];
			gpu_texture_t *icon   = any_map_get(box_projects_icon_map, handle);
#ifdef IRON_IOS
			if (icon != (gpu_texture_t *)&box_projects_no_icon) {
				gpu_delete_texture(icon);
			}
#endif
			free(handle);
		}
		array_free(keys);
		free(keys);
		map_free(box_projects_icon_map);
		box_projects_icon_map = NULL;
	}


	bool draggable;
#if defined(IRON_IOS)
	draggable = false;
#else
	draggable = true;
#endif

	i32 box_h = 400;
	ui_box_show_custom(&box_projects_show_box, 600, box_h, NULL, draggable, "");
}

void box_projects_recent_load(char *path) {
	if (!iron_file_exists(path)) {
		return;
	}
	box_projects_load(path);
}

void box_projects_recent_tab() {
	if (ui_tab(&box_projects_tab_index, tr("Recent"), true, -1, false)) {


		g_ui->enabled       = g_config->recent_projects->length > 0;
		bool was_typing     = g_ui->is_typing;
		box_projects_search = string_copy(ui_text_input(&box_projects_search, tr("Search"), UI_ALIGN_LEFT, true, true));
		g_ui->enabled       = true;
		char *first_path    = NULL; // Most recent project that passes the search filter
		for (i32 i = 0; i < g_config->recent_projects->length; ++i) {
			char *path = g_config->recent_projects->buffer[i];
			path = string_copy(string_replace_all(path, "\\", "/"));
			char *file = substring(path, string_last_index_of(path, PATH_SEP) + 1, string_length(path));

			if (string_index_of(to_lower_case(file), to_lower_case(box_projects_search)) < 0) {
				continue; // Search filter
			}
			if (first_path == NULL) {
				first_path = path;
			}
			if (ui_button(file, UI_ALIGN_LEFT, "")) {
				box_projects_recent_load(path);
			}
			if (g_ui->is_hovered) {
				ui_tooltip(path);
			}
		}

		if (first_path != NULL && !was_typing && g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_RETURN) {
			box_projects_recent_load(first_path);
		}

		g_ui->enabled = g_config->recent_projects->length > 0;
		if (ui_icon_button(tr("Clear"), ICON_ERASE, UI_ALIGN_LEFT)) {
			g_config->recent_projects = any_array_create_from_raw((void *[]){}, 0);
			config_save();
		}
		g_ui->enabled = true;

		ui_end_element();
		if (ui_icon_button(tr("New Project..."), ICON_FILE_NEW, UI_ALIGN_LEFT)) {
			project_new_box();
		}
		if (ui_icon_button(tr("Open..."), ICON_FOLDER_OPEN, UI_ALIGN_LEFT)) {
			project_open();
			ui_box_hide();
		}
	}
}
