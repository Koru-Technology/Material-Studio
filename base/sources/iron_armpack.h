
// .arm file format parser
// msgpack with typed arrays

#pragma once

#include "iron_array.h"
#include "iron_map.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void *armpack_decode(buffer_t *b);

void armpack_encode_start(void *encoded);
int  armpack_encode_end();
void armpack_encode_map(uint32_t count);
void armpack_encode_array(uint32_t count);
void armpack_encode_array_f32(f32_array_t *f32a);
void armpack_encode_array_i32(i32_array_t *i32a);
void armpack_encode_array_i16(i16_array_t *i16a);
void armpack_encode_array_u8(u8_array_t *u8a);
void armpack_encode_array_string(string_array_t *strings);
void armpack_encode_string(char *str);
void armpack_encode_i32(int32_t i);
void armpack_encode_f32(float f);
void armpack_encode_bool(bool b);
void armpack_encode_null();

uint32_t armpack_size_map();
uint32_t armpack_size_array();
uint32_t armpack_size_array_f32(f32_array_t *f32a);
uint32_t armpack_size_array_u8(u8_array_t *u8a);
uint32_t armpack_size_string(char *str);
uint32_t armpack_size_i32();
uint32_t armpack_size_f32();
uint32_t armpack_size_bool();

any_map_t *armpack_decode_to_map(buffer_t *b);
char      *armpack_decode_to_json(buffer_t *b);
float      armpack_map_get_f32(any_map_t *map, char *key);
int        armpack_map_get_i32(any_map_t *map, char *key);
void       armpack_map_set_i32(any_map_t *map, char *key, int value);
void       armpack_map_set_f32(any_map_t *map, char *key, float value);

// Schemas: a struct described by its saved fields, encoded as a map of them and decoded by key. Decoding matches keys
// by name: a key the schema has no field for is skipped, a field the file has no key for keeps its default (def for
// numbers, NULL for pointers), a value of another kind is skipped (numbers convert between i32, f32 and bool). Files
// written before a field was added, removed or moved decode as they are.
typedef enum armpack_kind {
	ARMPACK_I32,          // int32_t or uint32_t
	ARMPACK_F32,          // float
	ARMPACK_BOOL,         // bool
	ARMPACK_STRING,       // char *
	ARMPACK_F32_ARRAY,    // f32_array_t *
	ARMPACK_I32_ARRAY,    // i32_array_t * or u32_array_t *
	ARMPACK_I16_ARRAY,    // i16_array_t *
	ARMPACK_U8_ARRAY,     // u8_array_t * (buffer_t *)
	ARMPACK_STRING_ARRAY, // string_array_t *
	ARMPACK_U8_ARRAYS,    // Array of u8_array_t *
	ARMPACK_F32_ARRAYS,   // Array of f32_array_t *
	ARMPACK_STRUCTS,      // Array of pointers to structs of schema
} armpack_kind_t;

typedef struct armpack_schema armpack_schema_t;

typedef struct armpack_field {
	const char             *name;
	uint32_t                offset;
	armpack_kind_t          kind;
	const armpack_schema_t *schema; // ARMPACK_STRUCTS: of the elements
	float                   def;    // ARMPACK_I32, ARMPACK_F32, ARMPACK_BOOL: the value when the file has no key for it
} armpack_field_t;

struct armpack_schema {
	uint32_t               size; // sizeof the struct
	int                    count;
	const armpack_field_t *fields; // In the order they are written
};

// ARMPACK_FIELD(layer_data_t, parent, ARMPACK_I32, .def = -1)
#define ARMPACK_FIELD(T, f, k, ...) {.name = #f, .offset = (uint32_t)offsetof(T, f), .kind = k, __VA_ARGS__}
#define ARMPACK_SCHEMA(T, fs)       {.size = sizeof(T), .count = (int)(sizeof(fs) / sizeof(fs[0])), .fields = fs}

void    *armpack_decode_schema(buffer_t *b, const armpack_schema_t *schema); // NULL when b is not a valid map
void     armpack_encode_schema(void *s, const armpack_schema_t *schema);     // At the encode position, like armpack_encode_map
uint32_t armpack_size_schema(void *s, const armpack_schema_t *schema);       // Bytes armpack_encode_schema writes

void armpack_write_u8(uint8_t i);
void armpack_write_i16(int16_t i);
void armpack_write_u32(uint32_t i);
void armpack_write_i32(int32_t i);
void armpack_write_f32(float i);

/* JSON object:

    {
        name: "test",
        point: { x: 2, y: 4 },
        array: i32_array_create([1, 2, 3])
    }
*/

/* C struct:

    typedef struct point {
        int x;
        int y;
    } point_t;

    typedef struct test {
        char *name;
        point_t point;
        int32_array_t *array;
        // Optional pointer for storing runtime data
        void *_;
    } test_t;
*/

/*
    void encode_decode_test() {
        point_t a;
        a.x = 3;
        a.y = 9;

        uint32_t size = 0;
        size += armpack_size_map();
        size += armpack_size_string("x");
        size += armpack_size_i32();
        size += armpack_size_string("y");
        size += armpack_size_i32();

        void *encoded = malloc(size);
        armpack_encode_start(encoded);
        armpack_encode_map(2);
        armpack_encode_string("x");
        armpack_encode_i32(a.x);
        armpack_encode_string("y");
        armpack_encode_i32(a.y);

        buffer_t b = { .buffer = encoded, .length = size };
        point_t *decoded = armpack_decode(b);
    }
*/
