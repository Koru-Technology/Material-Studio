#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#define IRON_IOS
#else
#define IRON_MACOS
#endif

int kickstart(int argc, char **argv);

// Element count of a fixed size array (not a pointer)
#ifndef ARRAY_LENGTH
#define ARRAY_LENGTH(a) (sizeof(a) / sizeof((a)[0]))
#endif

typedef float    f32;
typedef double   f64;
typedef int8_t   i8;
typedef uint8_t  u8;
typedef int16_t  i16;
typedef uint16_t u16;
typedef int32_t  i32;
typedef uint32_t u32;
typedef int64_t  i64;
typedef uint64_t u64;
