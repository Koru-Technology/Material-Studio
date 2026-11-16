#include "global.h"

// Stand-in for the undo track's history_io.c (saved history steps), removed when it lands. Nothing is saved yet.

u8 *history_io_encode(u32 *len) {
	*len = 0;
	return NULL;
}

bool history_io_decode(u8 *data, u32 len) {
	return true;
}

i32 history_io_roots(hash128_t **out) {
	*out = NULL;
	return 0;
}

// The history thinned to fit budget bytes (the undo track's thinning)
void history_io_thin(i64 budget) {
}

// Steps and checkpoints dropped, timeline entries kept
void history_io_clear() {
}
