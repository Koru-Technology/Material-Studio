#pragma once

// The C interface the native (Swift) UI uses, imported through paint/native/Bridge/MaterialStudio-Bridging-Header.h.
// Self-contained on purpose: no engine headers, only plain functions and structs.

#include <stdbool.h>
#include <stdint.h>

static inline int app_api_version(void) {
	return 1;
}
