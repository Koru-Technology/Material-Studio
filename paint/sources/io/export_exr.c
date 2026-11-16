
#include "../global.h"

// Based on miniexr.cpp - public domain - 2013 Aras Pranckevicius / Unity Technologies
// https://github.com/aras-p/miniexr
// https://www.openexr.com/documentation/openexrfilelayout.pdf

static i32         _export_exr_width;
static i32         _export_exr_stride;
static u8_array_t *_export_exr_out;
static buffer_t   *_export_exr_src_view;
static void (*_export_exr_write_line)(i32);

static void export_exr_write_string(u8_array_t *out, char *str) {
	for (i32 i = 0; i < string_length(str); ++i) {
		u8_array_push(out, char_code_at(str, i));
	}
}

static void export_exr_write_line16(i32 byte_pos) {
	for (i32 x = 0; x < _export_exr_width; ++x) {
		u8_array_push(_export_exr_out, buffer_get_u8(_export_exr_src_view, byte_pos));
		u8_array_push(_export_exr_out, buffer_get_u8(_export_exr_src_view, byte_pos + 1));
		byte_pos += _export_exr_stride;
	}
}

static void export_exr_write_line32(i32 byte_pos) {
	for (i32 x = 0; x < _export_exr_width; ++x) {
		u8_array_push(_export_exr_out, buffer_get_u8(_export_exr_src_view, byte_pos));
		u8_array_push(_export_exr_out, buffer_get_u8(_export_exr_src_view, byte_pos + 1));
		u8_array_push(_export_exr_out, buffer_get_u8(_export_exr_src_view, byte_pos + 2));
		u8_array_push(_export_exr_out, buffer_get_u8(_export_exr_src_view, byte_pos + 3));
		byte_pos += _export_exr_stride;
	}
}

static void export_exr_write_bgr(i32 off, i32 pos, i32 byte_size) {
	_export_exr_write_line(pos + byte_size * 2);
	_export_exr_write_line(pos + byte_size);
	_export_exr_write_line(pos);
}

static void export_exr_write_single(i32 off, i32 pos, i32 byte_size) {
	_export_exr_write_line(pos + off * byte_size);
	_export_exr_write_line(pos + off * byte_size);
	_export_exr_write_line(pos + off * byte_size);
}

// The header and the line offset table of an uncompressed scanline file: B, G, R of half (bits 16) or float
static u8_array_t *export_exr_header(i32 width, i32 height, i32 bits) {
	u8_array_t *out = u8_array_create_from_raw((u8[]){}, 0);
	u8_array_push(out, 0x76); // magic
	u8_array_push(out, 0x2f);
	u8_array_push(out, 0x31);
	u8_array_push(out, 0x01);
	u8_array_push(out, 2); // version, scanline
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	export_exr_write_string(out, "channels");
	u8_array_push(out, 0);
	export_exr_write_string(out, "chlist");
	u8_array_push(out, 0);

	u8_array_push(out, 55);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);

	i32 attrib = bits == 16 ? 1 : 2; // half, float

	u8_array_push(out, char_code_at("B", 0)); // B
	u8_array_push(out, 0);

	u8_array_push(out, attrib);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);

	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);

	u8_array_push(out, 1);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);

	u8_array_push(out, 1);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);

	u8_array_push(out, char_code_at("G", 0)); // G
	u8_array_push(out, 0);

	u8_array_push(out, attrib);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);

	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);

	u8_array_push(out, 1);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);

	u8_array_push(out, 1);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);

	u8_array_push(out, char_code_at("R", 0)); // R
	u8_array_push(out, 0);

	u8_array_push(out, attrib);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);

	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);

	u8_array_push(out, 1);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);

	u8_array_push(out, 1);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);

	u8_array_push(out, 0);

	export_exr_write_string(out, "compression");
	u8_array_push(out, 0);
	export_exr_write_string(out, "compression");
	u8_array_push(out, 0);

	u8_array_push(out, 1);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0); // no compression

	export_exr_write_string(out, "dataWindow");
	u8_array_push(out, 0);
	export_exr_write_string(out, "box2i");
	u8_array_push(out, 0);

	u8_array_push(out, 16);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);

	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);

	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);

	i32 ww = width - 1;
	i32 hh = height - 1;

	u8_array_push(out, ww & 0xff);
	u8_array_push(out, (ww >> 8) & 0xff);
	u8_array_push(out, (ww >> 16) & 0xff);
	u8_array_push(out, (ww >> 24) & 0xff);

	u8_array_push(out, hh & 0xff);
	u8_array_push(out, (hh >> 8) & 0xff);
	u8_array_push(out, (hh >> 16) & 0xff);
	u8_array_push(out, (hh >> 24) & 0xff);

	export_exr_write_string(out, "displayWindow");
	u8_array_push(out, 0);
	export_exr_write_string(out, "box2i");
	u8_array_push(out, 0);

	u8_array_push(out, 16);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);

	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);

	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);

	u8_array_push(out, ww & 0xff);
	u8_array_push(out, (ww >> 8) & 0xff);
	u8_array_push(out, (ww >> 16) & 0xff);
	u8_array_push(out, (ww >> 24) & 0xff);

	u8_array_push(out, hh & 0xff);
	u8_array_push(out, (hh >> 8) & 0xff);
	u8_array_push(out, (hh >> 16) & 0xff);
	u8_array_push(out, (hh >> 24) & 0xff);

	export_exr_write_string(out, "lineOrder");
	u8_array_push(out, 0);
	export_exr_write_string(out, "lineOrder");
	u8_array_push(out, 0);

	u8_array_push(out, 1);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0); // increasing Y

	export_exr_write_string(out, "pixelAspectRatio");
	u8_array_push(out, 0);
	export_exr_write_string(out, "float");
	u8_array_push(out, 0);

	u8_array_push(out, 4);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);

	u8_array_push(out, 0); // 1.0f
	u8_array_push(out, 0);
	u8_array_push(out, 0x80);
	u8_array_push(out, 0x3f);

	export_exr_write_string(out, "screenWindowCenter");
	u8_array_push(out, 0);

	export_exr_write_string(out, "v2f");
	u8_array_push(out, 0);

	u8_array_push(out, 8);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);

	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);

	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);

	export_exr_write_string(out, "screenWindowWidth");
	u8_array_push(out, 0);

	export_exr_write_string(out, "float");
	u8_array_push(out, 0);

	u8_array_push(out, 4);
	u8_array_push(out, 0);
	u8_array_push(out, 0);
	u8_array_push(out, 0);

	u8_array_push(out, 0); // 1.0f
	u8_array_push(out, 0);
	u8_array_push(out, 0x80);
	u8_array_push(out, 0x3f);

	u8_array_push(out, 0); // end of header

	i32 byte_size             = bits == 16 ? 2 : 4;
	i64 k_header_size         = out->length;
	i64 k_scanline_table_size = 8 * (i64)height;
	i64 pixel_row_size        = (i64)width * 3 * byte_size;
	i64 full_row_size         = pixel_row_size + 8;

	// line offset table (64 bit offsets: past 2 GB at 16k and 32 bit)
	i64 ofs = k_header_size + k_scanline_table_size;
	for (i32 y = 0; y < height; ++y) {
		for (i32 b = 0; b < 8; ++b) {
			u8_array_push(out, (ofs >> (8 * b)) & 0xff);
		}
		ofs += full_row_size;
	}
	return out;
}

// Scanlines y0 to y0 + rows of the file appended to out, from src: rows x width texels of 4 channels (native order)
static void export_exr_rows(u8_array_t *out, buffer_t *src, i32 width, i32 y0, i32 rows, i32 bits, i32 type, i32 off) {
	i32 channels       = 4;
	i32 byte_size      = bits == 16 ? 2 : 4;
	i32 pixel_row_size = width * 3 * byte_size;
	i32 stride         = channels * byte_size;
	i32 pos            = 0;

	_export_exr_width                 = width;
	_export_exr_stride                = stride;
	_export_exr_out                   = out;
	_export_exr_src_view              = src;
	_export_exr_write_line            = bits == 16 ? export_exr_write_line16 : export_exr_write_line32;
	void (*write_data)(i32, i32, i32) = type == 1 || type == 3 ? export_exr_write_bgr : export_exr_write_single; // RGB1, RGBA

	for (i32 y = y0; y < y0 + rows; ++y) {
		// coordinate
		u8_array_push(out, y & 0xff);
		u8_array_push(out, (y >> 8) & 0xff);
		u8_array_push(out, (y >> 16) & 0xff);
		u8_array_push(out, (y >> 24) & 0xff);
		// data size
		u8_array_push(out, pixel_row_size & 0xff);
		u8_array_push(out, (pixel_row_size >> 8) & 0xff);
		u8_array_push(out, (pixel_row_size >> 16) & 0xff);
		u8_array_push(out, (pixel_row_size >> 24) & 0xff);
		// data
		write_data(off, pos, byte_size);
		pos += width * stride;
	}
}

buffer_t *export_exr_run(i32 width, i32 height, buffer_t *src, i32 bits, i32 type, i32 off) {
	u8_array_t *out = export_exr_header(width, height, bits);
	export_exr_rows(out, src, width, 0, height, bits, type, off);
	return out;
}

// Streaming (io/export_tiled.c): the header, then bands of scanlines top to bottom, each written as it comes
FILE *export_exr_begin(char *path, i32 width, i32 height, i32 bits) {
	FILE *f = fopen(path, "wb");
	if (f == NULL) {
		return NULL;
	}
	u8_array_t *h = export_exr_header(width, height, bits);
	fwrite(h->buffer, 1, h->length, f);
	array_free(h);
	free(h);
	return f;
}

void export_exr_band(FILE *f, buffer_t *src, i32 width, i32 y0, i32 rows, i32 bits, i32 type, i32 off) {
	u8_array_t *out = u8_array_create_from_raw((u8[]){}, 0);
	export_exr_rows(out, src, width, y0, rows, bits, type, off);
	fwrite(out->buffer, 1, out->length, f);
	array_free(out);
	free(out);
}
