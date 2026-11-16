
#include "iron_system.h"
#include "iron_alloc.h"
#include "iron_array.h"
#include "iron_draw.h"
#include "iron_file.h"
#include "iron_input.h"
#include "iron_map.h"
#include "iron_string.h"
#include <memory.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void iron_log_args(iron_log_level_t level, const char *format, va_list args) {

	char buffer[4096];
	vsnprintf(buffer, 4090, format, args);
	strcat(buffer, "\n");
	fprintf(level == IRON_LOG_LEVEL_INFO ? stdout : stderr, "%s", buffer);
}

void iron_log(const char *format, ...) {
	va_list args;
	va_start(args, format);
	iron_log_args(IRON_LOG_LEVEL_INFO, format == NULL ? "null" : format, args);
	va_end(args);
}

void iron_error(const char *format, ...) {
	{
		va_list args;
		va_start(args, format);
		iron_log_args(IRON_LOG_LEVEL_ERROR, format, args);
		va_end(args);
	}

}

double iron_time(void) {
	return iron_timestamp() / iron_frequency();
}

static void (*update_callback)(void)               = NULL;
static void (*foreground_callback)(void *)         = NULL;
static void *foreground_callback_data              = NULL;
static void (*background_callback)(void *)         = NULL;
static void *background_callback_data              = NULL;
static void (*shutdown_callback)(void *)           = NULL;
static void *shutdown_callback_data                = NULL;
static void (*drop_files_callback)(char *, void *) = NULL;
static void *drop_files_callback_data              = NULL;
static void (*cut_callback)(void *)                = NULL;
static void *cut_callback_data                     = NULL;
static void (*copy_callback)(void *)               = NULL;
static void *copy_callback_data                    = NULL;
static void (*paste_callback)(char *, void *)      = NULL;
static void *paste_callback_data                   = NULL;

#if defined(IRON_IOS) || defined(IRON_MACOS)
bool with_autoreleasepool(bool (*f)(void));
#endif

void iron_set_update_callback(void (*callback)(void)) {
	update_callback = callback;
}

void iron_set_foreground_callback(void (*callback)(void *), void *data) {
	foreground_callback      = callback;
	foreground_callback_data = data;
}

void iron_set_background_callback(void (*callback)(void *), void *data) {
	background_callback      = callback;
	background_callback_data = data;
}

void iron_set_shutdown_callback(void (*callback)(void *), void *data) {
	shutdown_callback      = callback;
	shutdown_callback_data = data;
}

void iron_set_drop_files_callback(void (*callback)(char *, void *), void *data) {
	drop_files_callback      = callback;
	drop_files_callback_data = data;
}

void iron_set_cut_callback(void (*callback)(void *), void *data) {
	cut_callback      = callback;
	cut_callback_data = data;
}

void iron_set_copy_callback(void (*callback)(void *), void *data) {
	copy_callback      = callback;
	copy_callback_data = data;
}

void iron_set_paste_callback(void (*callback)(char *, void *), void *data) {
	paste_callback      = callback;
	paste_callback_data = data;
}

void iron_internal_update_callback(void) {
	if (update_callback != NULL) {
		update_callback();
	}
}

void iron_internal_foreground_callback(void) {
	if (foreground_callback != NULL) {
		foreground_callback(foreground_callback_data);
	}
}

static void keyboard_release_all(void);

void iron_internal_background_callback(void) {
	keyboard_release_all();
	if (background_callback != NULL) {
		background_callback(background_callback_data);
	}
}

void iron_internal_shutdown_callback(void) {
	if (shutdown_callback != NULL) {
		shutdown_callback(shutdown_callback_data);
	}
}

// The system is low on memory, caches that are rebuilt on demand can go
static void (*memory_warning_callback)(void) = NULL;

void iron_set_memory_warning_callback(void (*callback)(void)) {
	memory_warning_callback = callback;
}

void iron_internal_memory_warning_callback(void) {
	if (memory_warning_callback != NULL) {
		memory_warning_callback();
	}
}

// The app is about to stop being active (home gesture, app switcher), the gpu can still be used
static void (*resign_active_callback)(void) = NULL;

void iron_set_resign_active_callback(void (*callback)(void)) {
	resign_active_callback = callback;
}

void iron_internal_resign_active_callback(void) {
	if (resign_active_callback != NULL) {
		resign_active_callback();
	}
}

// Files shared with the app, kept until the app listens for them (launched by sharing a file)
static void (*open_file_callback)(char *) = NULL;
static char *open_file_pending[8];
static int   open_file_pending_count = 0;

void iron_set_open_file_callback(void (*callback)(char *)) {
	open_file_callback = callback;
	for (int i = 0; i < open_file_pending_count; ++i) {
		callback(open_file_pending[i]);
		free(open_file_pending[i]);
	}
	open_file_pending_count = 0;
}

void iron_internal_open_file_callback(char *path) {
	if (open_file_callback != NULL) {
		open_file_callback(path);
	}
	else if (open_file_pending_count < 8) {
		open_file_pending[open_file_pending_count++] = strdup(path);
	}
}

void iron_internal_drop_files_callback(char *filePath) {
	if (drop_files_callback != NULL) {
		drop_files_callback(filePath, drop_files_callback_data);
	}
}

void iron_internal_cut_callback(void) {
	if (cut_callback != NULL) {
		cut_callback(cut_callback_data);
	}
}

void iron_internal_copy_callback(void) {
	if (copy_callback != NULL) {
		copy_callback(copy_callback_data);
	}
}

void iron_internal_paste_callback(char *value) {
	if (paste_callback != NULL) {
		paste_callback(value, paste_callback_data);
	}
}

static bool running                = false;
static char application_name[1024] = {"Iron Application"};

const char *iron_application_name(void) {
	return application_name;
}

void iron_set_app_name(const char *name) {
	snprintf(application_name, sizeof(application_name), "%s", name);
}

void iron_stop(void) {
	running = false;
}

bool iron_internal_frame(void) {
	iron_internal_update_callback();
	iron_internal_handle_messages();
	return running;
}

#ifdef IRON_EXTERNAL_LOOP
// The native app owns the run loop and calls these once per display refresh (paint/native/Engine/FrameDriver.swift)
bool iron_external_tick(void) {
	return with_autoreleasepool(iron_internal_frame);
}

void iron_external_shutdown(void) {
	iron_internal_shutdown();
}
#endif

void iron_start(void) {
	running = true;

#ifndef IRON_EXTERNAL_LOOP

#if defined(IRON_IOS) || defined(IRON_MACOS)
	while (with_autoreleasepool(iron_internal_frame)) {
	}
#else
	while (iron_internal_frame()) {
	}
#endif
	iron_internal_shutdown();
#endif
}

#ifdef IRON_IOS
void iron_copy_to_clipboard(const char *text) {
	iron_log("Oh no, iron_copy_to_clipboard is not implemented for this system.");
}
#endif

static void (*keyboard_key_down_callback)(int /*key_code*/, void * /*data*/)        = NULL;
static void *keyboard_key_down_callback_data                                        = NULL;
static void (*keyboard_key_up_callback)(int /*key_code*/, void * /*data*/)          = NULL;
static void *keyboard_key_up_callback_data                                          = NULL;
static void (*keyboard_key_press_callback)(unsigned /*character*/, void * /*data*/) = NULL;
static void *keyboard_key_press_callback_data                                       = NULL;

void iron_keyboard_set_key_down_callback(void (*value)(int /*key_code*/, void * /*data*/), void *data) {
	keyboard_key_down_callback      = value;
	keyboard_key_down_callback_data = data;
}

void iron_keyboard_set_key_up_callback(void (*value)(int /*key_code*/, void * /*data*/), void *data) {
	keyboard_key_up_callback      = value;
	keyboard_key_up_callback_data = data;
}

void iron_keyboard_set_key_press_callback(void (*value)(unsigned /*character*/, void * /*data*/), void *data) {
	keyboard_key_press_callback      = value;
	keyboard_key_press_callback_data = data;
}

static bool keyboard_keys_held[256];

void iron_internal_keyboard_trigger_key_down(int key_code) {
	if (key_code >= 0 && key_code < 256) {
		keyboard_keys_held[key_code] = true;
	}
	if (keyboard_key_down_callback != NULL) {
		keyboard_key_down_callback(key_code, keyboard_key_down_callback_data);
	}
}

void iron_internal_keyboard_trigger_key_up(int key_code) {
	if (key_code >= 0 && key_code < 256) {
		keyboard_keys_held[key_code] = false;
	}
	if (keyboard_key_up_callback != NULL) {
		keyboard_key_up_callback(key_code, keyboard_key_up_callback_data);
	}
}

static void keyboard_release_all(void) {
	for (int i = 0; i < 256; ++i) {
		if (keyboard_keys_held[i]) {
			iron_internal_keyboard_trigger_key_up(i);
		}
	}
}

void iron_internal_keyboard_trigger_key_press(unsigned character) {
	if (keyboard_key_press_callback != NULL) {
		keyboard_key_press_callback(character, keyboard_key_press_callback_data);
	}
}

static void (*mouse_press_callback)(int /*button*/, int /*x*/, int /*y*/, void * /*data*/)                      = NULL;
static void *mouse_press_callback_data                                                                          = NULL;
static void (*mouse_release_callback)(int /*button*/, int /*x*/, int /*y*/, void * /*data*/)                    = NULL;
static void *mouse_release_callback_data                                                                        = NULL;
static void (*mouse_move_callback)(int /*x*/, int /*y*/, int /*movementX*/, int /*movementY*/, void * /*data*/) = NULL;
static void *mouse_move_callback_data                                                                           = NULL;
static void (*mouse_scroll_callback)(float /*delta*/, void * /*data*/)                                          = NULL;
static void *mouse_scroll_callback_data                                                                         = NULL;

void iron_mouse_set_press_callback(void (*value)(int /*button*/, int /*x*/, int /*y*/, void * /*data*/), void *data) {
	mouse_press_callback      = value;
	mouse_press_callback_data = data;
}

void iron_mouse_set_release_callback(void (*value)(int /*button*/, int /*x*/, int /*y*/, void * /*data*/), void *data) {
	mouse_release_callback      = value;
	mouse_release_callback_data = data;
}

void iron_mouse_set_move_callback(void (*value)(int /*x*/, int /*y*/, int /*movement_x*/, int /*movement_y*/, void * /*data*/), void *data) {
	mouse_move_callback      = value;
	mouse_move_callback_data = data;
}

void iron_mouse_set_scroll_callback(void (*value)(float /*delta*/, void * /*data*/), void *data) {
	mouse_scroll_callback      = value;
	mouse_scroll_callback_data = data;
}

void iron_internal_mouse_trigger_release(int button, int x, int y) {
	if (mouse_release_callback != NULL) {
		mouse_release_callback(button, x, y, mouse_release_callback_data);
	}
}

void iron_internal_mouse_trigger_scroll(float delta) {
	if (mouse_scroll_callback != NULL) {
		mouse_scroll_callback(delta, mouse_scroll_callback_data);
	}
}

static bool moved    = false;
static bool locked   = false;
static int  preLockX = 0;
static int  preLockY = 0;
static int  lastX    = 0;
static int  lastY    = 0;

bool iron_mouse_trackpad_drag = false; // Two finger trackpad drag acts as right mouse button
bool iron_mouse_movement_only = false; // Move callback reports movement without changing the position

void iron_internal_mouse_trigger_press(int button, int x, int y) {
	lastX = x;
	lastY = y;
	if (mouse_press_callback != NULL) {
		mouse_press_callback(button, x, y, mouse_press_callback_data);
	}
}

void iron_internal_mouse_trigger_move(int x, int y) {
	int movementX = 0;
	int movementY = 0;
	if (iron_mouse_is_locked()) {
		movementX = x - preLockX;
		movementY = y - preLockY;
		if (movementX != 0 || movementY != 0) {
			iron_mouse_set_position(preLockX, preLockY);
			x = preLockX;
			y = preLockY;
		}
	}
	else if (moved) {
		movementX = x - lastX;
		movementY = y - lastY;
	}
	moved = true;

	lastX = x;
	lastY = y;
	if (mouse_move_callback != NULL && (movementX != 0 || movementY != 0)) {
		mouse_move_callback(x, y, movementX, movementY, mouse_move_callback_data);
	}
}

void iron_internal_mouse_trigger_movement(int movement_x, int movement_y) {
	if (mouse_move_callback != NULL && (movement_x != 0 || movement_y != 0)) {
		iron_mouse_movement_only = true;
		mouse_move_callback(lastX, lastY, movement_x, movement_y, mouse_move_callback_data);
		iron_mouse_movement_only = false;
	}
}

bool iron_mouse_is_locked(void) {
	return locked;
}

void iron_mouse_lock() {
	if (iron_mouse_is_locked() || !iron_mouse_can_lock()) {
		return;
	}
	locked = true;
	iron_mouse_get_position(&preLockX, &preLockY);
	iron_mouse_hide();
}

void iron_mouse_unlock(void) {
	if (!iron_mouse_is_locked() || !iron_mouse_can_lock()) {
		return;
	}
	moved  = false;
	locked = false;
	iron_mouse_set_position(preLockX, preLockY);
	iron_mouse_show();
}

static void (*pen_press_callback)(int /*x*/, int /*y*/, float /*pressure*/)   = NULL;
static void (*pen_move_callback)(int /*x*/, int /*y*/, float /*pressure*/)    = NULL;
static void (*pen_release_callback)(int /*x*/, int /*y*/, float /*pressure*/) = NULL;

void iron_pen_set_press_callback(void (*value)(int /*x*/, int /*y*/, float /*pressure*/)) {
	pen_press_callback = value;
}

void iron_pen_set_move_callback(void (*value)(int /*x*/, int /*y*/, float /*pressure*/)) {
	pen_move_callback = value;
}

void iron_pen_set_release_callback(void (*value)(int /*x*/, int /*y*/, float /*pressure*/)) {
	pen_release_callback = value;
}

void iron_internal_pen_trigger_press(int x, int y, float pressure) {
	if (pen_press_callback != NULL) {
		pen_press_callback(x, y, pressure);
	}
}

void iron_internal_pen_trigger_move(int x, int y, float pressure) {
	if (pen_move_callback != NULL) {
		pen_move_callback(x, y, pressure);
	}
}

void iron_internal_pen_trigger_sample(float x, float y, float pressure, float altitude, float azimuth, float roll, double time) {
	pen_sample_listener(x, y, pressure, altitude, azimuth, roll, time);
}

void iron_internal_pen_trigger_hover(float x, float y, float height) {
	pen_hover_listener(x, y, height);
}

void iron_internal_pen_trigger_predicted(float x, float y) {
	pen_predicted_listener(x, y);
}

void iron_internal_mouse_trigger_twist(float radians) {
#ifdef IRON_IOS
	mouse_twist_listener(radians);
#endif
}

static bool (*pointer_hidden_callback)(float, float) = NULL;

void iron_set_pointer_hidden_callback(bool (*callback)(float x, float y)) {
	pointer_hidden_callback = callback;
}

bool iron_internal_pointer_hidden(float x, float y) {
	return pointer_hidden_callback != NULL && pointer_hidden_callback(x, y);
}

static void (*pencil_gesture_callback)(int, float, float) = NULL;

void iron_set_pencil_gesture_callback(void (*callback)(int gesture, float x, float y)) {
	pencil_gesture_callback = callback;
}

void iron_internal_pencil_gesture_callback(int gesture, float x, float y) {
	if (pencil_gesture_callback != NULL) {
		pencil_gesture_callback(gesture, x, y);
	}
}

#ifndef IRON_IOS
int iron_pencil_preferred_action(int gesture) {
	return IRON_PENCIL_PREFERS_IGNORE;
}

void iron_haptic_tick(float x, float y) {}
#endif

void iron_internal_pen_trigger_release(int x, int y, float pressure) {
	if (pen_release_callback != NULL) {
		pen_release_callback(x, y, pressure);
	}
}

static void (*surface_touch_start_callback)(int /*index*/, int /*x*/, int /*y*/) = NULL;
static void (*surface_move_callback)(int /*index*/, int /*x*/, int /*y*/)        = NULL;
static void (*surface_touch_end_callback)(int /*index*/, int /*x*/, int /*y*/)   = NULL;

void iron_surface_set_touch_start_callback(void (*value)(int /*index*/, int /*x*/, int /*y*/)) {
	surface_touch_start_callback = value;
}

void iron_surface_set_move_callback(void (*value)(int /*index*/, int /*x*/, int /*y*/)) {
	surface_move_callback = value;
}

void iron_surface_set_touch_end_callback(void (*value)(int /*index*/, int /*x*/, int /*y*/)) {
	surface_touch_end_callback = value;
}

void iron_internal_surface_trigger_touch_start(int index, int x, int y) {
	if (surface_touch_start_callback != NULL) {
		surface_touch_start_callback(index, x, y);
	}
}

void iron_internal_surface_trigger_move(int index, int x, int y) {
	if (surface_move_callback != NULL) {
		surface_move_callback(index, x, y);
	}
}

void iron_internal_surface_trigger_touch_end(int index, int x, int y) {
	if (surface_touch_end_callback != NULL) {
		surface_touch_end_callback(index, x, y);
	}
}


i32 iron_sys_command(char *cmd) {
#ifdef IRON_IOS
	int result = 0;
#else
	int result = system(cmd);
#endif
	return result;
}

#ifdef WITH_NFD

#include <nfd.h>

string_array_t *iron_open_dialog(char *filter_list, char *default_path, bool open_multiple) {
	nfdpathset_t out_paths;
	nfdchar_t   *out_path = NULL;
	nfdresult_t  result   = open_multiple ? NFD_OpenDialogMultiple(filter_list, default_path, &out_paths) : NFD_OpenDialog(filter_list, default_path, &out_path);

	if (result == NFD_OKAY) {
		int             path_count = open_multiple ? (int)NFD_PathSet_GetCount(&out_paths) : 1;
		string_array_t *result     = (string_array_t *)any_array_create(path_count);

		if (open_multiple) {
			for (int i = 0; i < path_count; ++i) {
				nfdchar_t *out_path = NFD_PathSet_GetPath(&out_paths, i);
				result->buffer[i]   = out_path;
			}
			// NFD_PathSet_Free(&out_paths);
		}
		else {
			result->buffer[0] = out_path;
			// free(out_path);
		}
		return result;
	}
	return NULL;
}

static char iron_save_dialog_path[1024];

char *iron_save_dialog(char *filter_list, char *default_path) {
	nfdchar_t  *out_path = NULL;
	nfdresult_t result   = NFD_SaveDialog(filter_list, default_path, &out_path);
	if (result == NFD_OKAY) {
		snprintf(iron_save_dialog_path, sizeof(iron_save_dialog_path), "%s", out_path);
		free(out_path);
		return iron_save_dialog_path;
	}
	return NULL;
}

#elif defined(IRON_IOS)

#include "backends/ios_file_dialog.h"
#include <wchar.h>

extern char temp_string[1024 * 128];

string_array_t *iron_open_dialog(char *filter_list, char *default_path, bool open_multiple) {
	// Once finished drop_files callback is called
	IOSFileDialogOpen();
	return NULL;
}

char *iron_save_dialog(char *filter_list, char *default_path) {
	// Path to app document directory
	wchar_t *out_path = IOSFileDialogSave();
	wcstombs(temp_string, out_path, sizeof(temp_string));
	return temp_string;
}

#endif

void          _iron_init(iron_window_options_t *ops);
void          _iron_set_update_callback(void (*callback)(void));
void          _iron_set_drop_files_callback(void (*callback)(char *));
void          iron_set_application_state_callback(void (*on_foreground)(void), void (*on_background)(void), void (*on_shutdown)(void));
void          iron_set_keyboard_down_callback(void (*callback)(int));
void          iron_set_keyboard_up_callback(void (*callback)(int));
void          iron_set_mouse_down_callback(void (*callback)(int, int, int));
void          iron_set_mouse_up_callback(void (*callback)(int, int, int));
void          iron_set_mouse_move_callback(void (*callback)(int, int, int, int));
void          iron_set_mouse_wheel_callback(void (*callback)(float));
void          iron_set_touch_down_callback(void (*callback)(int, int, int));
void          iron_set_touch_up_callback(void (*callback)(int, int, int));
void          iron_set_touch_move_callback(void (*callback)(int, int, int));
void          iron_set_pen_down_callback(void (*callback)(int, int, float));
void          iron_set_pen_up_callback(void (*callback)(int, int, float));
void          iron_set_pen_move_callback(void (*callback)(int, int, float));
buffer_t     *iron_load_blob(char *file);
i32           iron_display_width(i32 index);
i32           iron_display_height(i32 index);
i32           iron_display_frequency(i32 index);
i32           iron_display_ppi(i32 index);
bool          iron_display_is_primary(i32 index);
gpu_shader_t *gpu_create_shader(buffer_t *data, i32 shader_type);
void          iron_delete_blob(buffer_t *buffer);
char         *data_path(void);


any_map_t   *_sys_shaders    = NULL;
f64          _sys_start_time = 0.0;
char         _sys_window_title[1024];
any_array_t *_sys_foreground_listeners = NULL;
any_array_t *_sys_background_listeners = NULL;
any_array_t *_sys_shutdown_listeners   = NULL;
any_array_t *_sys_drop_files_listeners = NULL;
any_array_t *_sys_on_next_frames       = NULL;
any_array_t *_sys_on_end_frames        = NULL;
any_array_t *_sys_on_updates           = NULL;
i32          _sys_lastw                = -1;
i32          _sys_lasth                = -1;

void (*sys_on_resize)(void) = NULL;
i32 (*sys_on_w)(void)       = NULL;
i32 (*sys_on_h)(void)       = NULL;
i32 (*sys_on_x)(void)       = NULL;
i32 (*sys_on_y)(void)       = NULL;

static f64 _sys_time_last       = 0.0;
static f64 _sys_time_real_delta = 0.0;
static i32 _sys_time_frequency  = -1;

static sys_callback_t *_sys_callback_create(void (*f)(void)) {
	sys_callback_t *cb = calloc(1, sizeof(sys_callback_t));
	cb->f              = f;
	return cb;
}

static callback_t *_callback_create(void (*f)(void *data), void *data) {
	callback_t *cb = calloc(1, sizeof(callback_t));
	cb->f          = f;
	cb->data       = data;
	return cb;
}

void sys_start(iron_window_options_t *ops) {
	_sys_foreground_listeners = any_array_create(0);
	_sys_background_listeners = any_array_create(0);
	_sys_shutdown_listeners   = any_array_create(0);
	_sys_drop_files_listeners = any_array_create(0);
	_sys_on_next_frames       = any_array_create(0);
	_sys_on_end_frames        = any_array_create(0);
	_sys_on_updates           = any_array_create(0);
	_sys_shaders              = any_map_create();

	_iron_init(ops);

	_sys_start_time = iron_time();

	char *dp  = data_path();
	char *ext = sys_shader_ext();

	char     *draw_shaders[8] = {"draw_image.vert", "draw_image.frag", "draw_rect.vert", "draw_rect.frag",
	                              "draw_tris.vert",  "draw_tris.frag",  "draw_text.vert", "draw_text.frag"};
	buffer_t *draw_blobs[8];
	for (int i = 0; i < 8; ++i) {
		char path[512];
		snprintf(path, sizeof(path), "%s%s%s", dp, draw_shaders[i], ext);
		draw_blobs[i] = iron_load_blob(path);
	}
	draw_init(draw_blobs[0], draw_blobs[1], draw_blobs[2], draw_blobs[3], draw_blobs[4], draw_blobs[5], draw_blobs[6], draw_blobs[7]);
	for (int i = 0; i < 8; ++i) {
		iron_delete_blob(draw_blobs[i]);
	}

	_iron_set_update_callback(sys_render);
	_iron_set_drop_files_callback(sys_drop_files_callback);
	iron_set_application_state_callback(sys_foreground_callback, sys_background_callback, sys_shutdown_callback);
	iron_set_keyboard_down_callback(sys_keyboard_down_callback);
	iron_set_keyboard_up_callback(sys_keyboard_up_callback);
	iron_set_mouse_down_callback(sys_mouse_down_callback);
	iron_set_mouse_up_callback(sys_mouse_up_callback);
	iron_set_mouse_move_callback(sys_mouse_move_callback);
	iron_set_mouse_wheel_callback(sys_mouse_wheel_callback);
	iron_set_touch_down_callback(sys_touch_down_callback);
	iron_set_touch_up_callback(sys_touch_up_callback);
	iron_set_touch_move_callback(sys_touch_move_callback);
	iron_set_pen_down_callback(sys_pen_down_callback);
	iron_set_pen_up_callback(sys_pen_up_callback);
	iron_set_pen_move_callback(sys_pen_move_callback);
	input_register();
}

void sys_notify_on_app_state(void (*on_foreground)(void), void (*on_background)(void), void (*on_shutdown)(void)) {
	if (on_foreground != NULL) {
		any_array_push(_sys_foreground_listeners, _sys_callback_create(on_foreground));
	}
	if (on_background != NULL) {
		any_array_push(_sys_background_listeners, _sys_callback_create(on_background));
	}
	if (on_shutdown != NULL) {
		any_array_push(_sys_shutdown_listeners, _sys_callback_create(on_shutdown));
	}
}

void sys_notify_on_drop_files(void (*drop_files_listener)(char *s)) {
	sys_string_callback_t *cb = calloc(1, sizeof(sys_string_callback_t));
	cb->f                     = drop_files_listener;
	any_array_push(_sys_drop_files_listeners, cb);
}

void sys_foreground(void) {
	for (i32 i = 0; i < (i32)_sys_foreground_listeners->length; ++i) {
		sys_callback_t *cb = _sys_foreground_listeners->buffer[i];
		cb->f();
	}
	input_on_foreground();
}

void sys_background(void) {
	for (i32 i = 0; i < (i32)_sys_background_listeners->length; ++i) {
		sys_callback_t *cb = _sys_background_listeners->buffer[i];
		cb->f();
	}
}

void sys_shutdown(void) {
	for (i32 i = 0; i < (i32)_sys_shutdown_listeners->length; ++i) {
		sys_callback_t *cb = _sys_shutdown_listeners->buffer[i];
		cb->f();
	}
}

void sys_drop_files(char *file_path) {
	for (i32 i = 0; i < (i32)_sys_drop_files_listeners->length; ++i) {
		sys_string_callback_t *cb = _sys_drop_files_listeners->buffer[i];
		cb->f(file_path);
	}
}

f64 sys_time(void) {
	return iron_time() - _sys_start_time;
}

void sys_drop_files_callback(char *file_path) {
	sys_drop_files(file_path);
}

void sys_foreground_callback(void) {
	sys_foreground();
}

void sys_background_callback(void) {
	sys_background();
}

void sys_shutdown_callback(void) {
	sys_shutdown();
}

void sys_keyboard_down_callback(i32 code) {
	keyboard_down_listener(code);
}

void sys_keyboard_up_callback(i32 code) {
	keyboard_up_listener(code);
}

void sys_mouse_down_callback(i32 button, i32 x, i32 y) {
	mouse_down_listener(button, x, y);
}

void sys_mouse_up_callback(i32 button, i32 x, i32 y) {
	mouse_up_listener(button, x, y);
}

void sys_mouse_move_callback(i32 x, i32 y, i32 mx, i32 my) {
	mouse_move_listener(x, y, mx, my);
}

void sys_mouse_wheel_callback(f32 delta) {
	mouse_wheel_listener(delta);
}

void sys_touch_down_callback(i32 index, i32 x, i32 y) {
#ifdef IRON_IOS
	mouse_on_touch_down(index, x, y);
#endif
}

void sys_touch_up_callback(i32 index, i32 x, i32 y) {
#ifdef IRON_IOS
	mouse_on_touch_up(index, x, y);
#endif
}

void sys_touch_move_callback(i32 index, i32 x, i32 y) {
#ifdef IRON_IOS
	mouse_on_touch_move(index, x, y);
#endif
}

void sys_pen_down_callback(i32 x, i32 y, f32 pressure) {
	pen_down_listener(x, y, pressure);
}

void sys_pen_up_callback(i32 x, i32 y, f32 pressure) {
	pen_up_listener(x, y, pressure);
}

void sys_pen_move_callback(i32 x, i32 y, f32 pressure) {
	pen_move_listener(x, y, pressure);
}


char *sys_title(void) {
	return _sys_window_title;
}

void sys_title_set(char *value) {
	iron_window_set_title(value);
	snprintf(_sys_window_title, sizeof(_sys_window_title), "%s", value);
}

i32 sys_display_primary_id(void) {
	for (i32 i = 0; i < iron_count_displays(); ++i) {
		if (iron_display_is_primary(i)) {
			return i;
		}
	}
	return 0;
}

i32 sys_display_width(void) {
	return iron_display_width(sys_display_primary_id());
}

i32 sys_display_height(void) {
	return iron_display_height(sys_display_primary_id());
}

i32 sys_display_frequency(void) {
	return iron_display_frequency(sys_display_primary_id());
}

i32 sys_display_ppi(void) {
	return iron_display_ppi(sys_display_primary_id());
}

char *sys_shader_ext(void) {
	return ".metal";
}

gpu_shader_t *sys_get_shader(char *name) {
	gpu_shader_t *shader = any_map_get(_sys_shaders, name);
	if (shader == NULL) {
		char  path[512];
		char *dp  = data_path();
		char *ext = sys_shader_ext();
		snprintf(path, sizeof(path), "%s%s%s", dp, name, ext);
		gpu_shader_type_t shader_type = ends_with(name, ".frag") ? GPU_SHADER_TYPE_FRAGMENT : GPU_SHADER_TYPE_VERTEX;
		buffer_t         *blob        = iron_load_blob(path);
		shader                        = gpu_create_shader(blob, (i32)shader_type);
		iron_delete_blob(blob);
		any_map_set(_sys_shaders, name, shader);
	}
	return shader;
}

i32 sys_w(void) {
	if (sys_on_w != NULL) {
		return sys_on_w();
	}
	return iron_window_width();
}

i32 sys_h(void) {
	if (sys_on_h != NULL) {
		return sys_on_h();
	}
	return iron_window_height();
}

// Width over height of the window, 1 while it has no height
f32 sys_aspect(void) {
	i32 h = sys_h();
	return h > 0 ? sys_w() / (f32)h : 1.0f;
}

i32 sys_x(void) {
	if (sys_on_x != NULL) {
		return sys_on_x();
	}
	return 0;
}

i32 sys_y(void) {
	if (sys_on_y != NULL) {
		return sys_on_y();
	}
	return 0;
}

f64 sys_delta(void) {
	if (_sys_time_frequency < 0) {
		_sys_time_frequency = sys_display_frequency();
	}
	return 1.0 / (f64)_sys_time_frequency;
}

f64 sys_real_delta(void) {
	return _sys_time_real_delta;
}

static void _sys_run_callbacks(any_array_t *cbs, i32 len) {
	for (i32 i = 0; i < len; ++i) {
		callback_t *cb = cbs->buffer[i];
		if (cb != NULL) { // Callback may have been removed
			cb->f(cb->data);
		}
	}
}

static void _sys_run_callbacks_once(any_array_t *cbs, i32 len) {
	for (i32 i = 0; i < len; ++i) {
		callback_t *cb = cbs->buffer[i];
		if (cb != NULL) {
			cb->f(cb->data);
			free(cb);
		}
	}
	array_splice(cbs, 0, len);
}

void sys_render(void) {
	i32 len = _sys_on_next_frames->length;
	_sys_run_callbacks_once(_sys_on_next_frames, len);

	scene_render_frame();
	_sys_run_callbacks(_sys_on_updates, _sys_on_updates->length);

	len = _sys_on_end_frames->length;
	_sys_run_callbacks_once(_sys_on_end_frames, len);

	input_end_frame();

	// Rebuild projection on window resize
	if (_sys_lastw == -1) {
		_sys_lastw = sys_w();
		_sys_lasth = sys_h();
	}
	if (_sys_lastw != sys_w() || _sys_lasth != sys_h()) {
		if (sys_on_resize != NULL) {
			sys_on_resize();
		}
	}
	_sys_lastw           = sys_w();
	_sys_lasth           = sys_h();
	_sys_time_real_delta = sys_time() - _sys_time_last;
	_sys_time_last       = sys_time();
}

// Hooks

void sys_notify_on_update(void (*f)(void *data), void *data) {
	any_array_push(_sys_on_updates, _callback_create(f, data));
}

void sys_notify_on_next_frame(void (*f)(void *data), void *data) {
	any_array_push(_sys_on_next_frames, _callback_create(f, data));
}

void sys_notify_on_end_frame(void (*f)(void *data), void *data) {
	any_array_push(_sys_on_end_frames, _callback_create(f, data));
}

static void _sys_remove_callback(any_array_t *ar, void (*f)(void *data)) {
	for (i32 i = 0; i < (i32)ar->length; ++i) {
		callback_t *cb = ar->buffer[i];
		if (cb->f == f) {
			array_splice(ar, i, 1);
			break;
		}
	}
}

void sys_remove_update(void (*f)(void *data)) {
	_sys_remove_callback(_sys_on_updates, f);
}

char *sys_buffer_to_string(buffer_t *b) {
	if (b == NULL) {
		return NULL;
	}
	char *str = string_alloc(b->length + 1);
	memcpy(str, b->buffer, b->length);
	return str;
}

buffer_t *sys_string_to_buffer(char *str) {
	u8_array_t *b = u8_array_create(string_length(str));
	memcpy(b->buffer, str, string_length(str));
	return b;
}
