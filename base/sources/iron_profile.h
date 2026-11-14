#pragma once

#include <stdbool.h>

// Sampling profiler, see iron_profile.c. Off unless the env IRON_PROFILE is set.

// A named point in time, to cut the samples into phases
void iron_profile_mark(const char *name);
// Stops sampling and writes the file (also done at exit and after IRON_PROFILE_SECONDS)
void iron_profile_stop(void);
bool iron_profile_enabled(void);
