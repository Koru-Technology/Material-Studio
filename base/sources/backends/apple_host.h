#pragma once

// A native (SwiftUI) app hosting the engine on macOS / iOS, built with IRON_EXTERNAL_LOOP.
// The app owns the run loop: it embeds the engine view, starts the engine once the view is laid out,
// then calls iron_external_tick() once per display refresh. Self-contained: imported by Swift.

#include <stdbool.h>

// The engine's Metal view, created on first use (macOS: NSView, iOS: UIViewController whose view is the canvas)
void *iron_apple_engine_view(void);
void *iron_apple_engine_view_controller(void);

// Runs the app's startup (kickstart with the process arguments), returns once the engine is running
void iron_apple_start(void);

// One frame: update, render, present. False once the engine stopped (a script quit, the window closed)
bool iron_external_tick(void);
void iron_external_shutdown(void);

// Idle: the engine sleeps a moment after the last input or change, unless work spread over frames is under way. The
// host stops its frames while it sleeps and resumes them when the wake callback is called (input, an app_* call, a file
// opened, a resize), on the main thread. iron_apple_idle_check, now and then while asleep: true when time driven work
// (an autosave, the session journal) woke it.
bool iron_apple_asleep(void);
void iron_apple_set_wake_callback(void (*callback)(void));
bool iron_apple_idle_check(void);

// macOS: the app is asked to quit. Runs the save prompt; true: the engine stopped, quit now
bool iron_apple_should_terminate(void);

// iOS scene lifecycle
void iron_apple_resign_active(void);
void iron_apple_enter_background(void);
void iron_apple_enter_foreground(void);
void iron_apple_open_url(const char *url); // A file shared with the app (Open in / Share sheet)
bool iron_apple_take_picker_request(void);  // A file or folder picker was asked for since the last call
