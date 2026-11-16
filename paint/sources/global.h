
#pragma once

#include <iron.h>

// main.c includes this header, then every .c file of the app: they are one translation unit. globals.h and
// shader_snippets.h define variables with their initial values, so only main.c includes this header (startup.c,
// minic_api.c and material_maker/mm_*.c are compiled on their own and declare what they use).
#include "enums.h"
#include "functions.h" // Includes types.h itself, the prototypes use its structs
#include "globals.h"
#include "shader_snippets.h"
#include "types.h"
