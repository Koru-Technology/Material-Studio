// Cloud materials: the Poly Haven and ambientCG catalogs, browsed in the Library tab.
// - Catalog: fetched once and cached in the save path, refreshed in the background after a week.
// - Thumbnails: downloaded for the visible items only, decoded on a thread, the ones not seen for the longest are unloaded.
// - Materials: tapping one downloads its textures at the size picked in Preferences > Library into the app storage,
//   then adds it to the project like a folder import. ambientCG ships zips, they are extracted on a thread.

#include "global.h"
#define JSMN_HEADER
#include <dirent.h>
#include <iron_thread.h>
#include <jsmn.h>
#include <sinfl.h>
#include <sys/stat.h>
#include <time.h>

#define CLOUD_PH                  0
#define CLOUD_ACG                 1
#define CLOUD_MAX_ICONS           400
#define CLOUD_MAX_THUMB_DOWNLOADS 4
#define CLOUD_CATALOG_MAX_AGE     (7 * 24 * 60 * 60)
#define CLOUD_TYPES               14

struct cloud_item { // cloud_item_t, types.h
	i32            source;
	char          *id;
	char          *name;
	char          *search; // Lowercase name, id and tags
	i32            type;
	bool           downloaded; // Textures are in the app storage at the current size
	bool           busy;       // Waiting for or being downloaded
	gpu_texture_t *icon;
	i32            icon_state; // 0 - none, 1 - downloading, 2 - file ready, 3 - decoding, 4 - loaded, 5 - failed
	i32            last_seen;
};

static char *cloud_type_names[CLOUD_TYPES] = {"Wood",
                                              "Metal",
                                              "Stone",
                                              "Brick & Block",
                                              "Concrete",
                                              "Ground & Terrain",
                                              "Asphalt & Bitumen",
                                              "Ceramic & Tiles",
                                              "Textiles & Leather",
                                              "Plastic & Rubber",
                                              "Paper & Card",
                                              "Organic",
                                              "Decals & Imperfections",
                                              "Other"};

static char *cloud_res_names[] = {"1K", "2K", "4K", "8K"};

static cloud_item_t **cloud_items = NULL; // Main thread, sorted by type then name
static i32            cloud_count = 0;

// ██████╗ ███████╗ ██████╗ ██╗   ██╗███████╗███████╗████████╗
// Requests report to the main thread

static void cloud_request(const char *url, const char *dst, iron_https_callback_t cb, void *data) {
	const char *host  = url + 8; // https://
	const char *slash = strchr(host, '/');
	char        base[256];
	size_t      lb = slash != NULL ? (size_t)(slash - host) : strlen(host);
	if (lb > sizeof(base) - 1) {
		lb = sizeof(base) - 1;
	}
	memcpy(base, host, lb);
	base[lb] = 0;
	if (dst != NULL) {
		remove(dst); // The download is moved there, which fails when the file exists
	}
	iron_net_request(base, slash != NULL ? slash + 1 : "", NULL, 443, IRON_HTTPS_GET, NULL, cb, data, dst);
}

// The body is not freed by the iOS backend
static void cloud_free_body(const char *body) {
#ifdef IRON_IOS
	free((void *)body);
#else
	(void)body;
#endif
}

static char *cloud_dir() {
	return string("%scloud", iron_internal_save_path());
}

static bool cloud_file_starts_with(const char *path, const unsigned char *magic, i32 n) {
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		return false;
	}
	unsigned char buf[8] = {0};
	size_t        r      = fread(buf, 1, n, f);
	fclose(f);
	return r == (size_t)n && memcmp(buf, magic, n) == 0;
}

static bool cloud_is_image(const char *path) {
	static const unsigned char jpg[] = {0xff, 0xd8};
	static const unsigned char png[] = {0x89, 'P', 'N', 'G'};
	return cloud_file_starts_with(path, jpg, 2) || cloud_file_starts_with(path, png, 4);
}

static void cloud_remove_tree(const char *path) {
	DIR *d = opendir(path);
	if (d == NULL) {
		remove(path);
		return;
	}
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
			continue;
		}
		char *sub = library_join(path, e->d_name);
		cloud_remove_tree(sub);
		free(sub);
	}
	closedir(d);
	rmdir(path);
}

static f64 cloud_tree_size(const char *path) {
	struct stat st;
	if (stat(path, &st) != 0) {
		return 0;
	}
	if (!S_ISDIR(st.st_mode)) {
		return (f64)st.st_size;
	}
	f64  size = 0;
	DIR *d    = opendir(path);
	if (d == NULL) {
		return 0;
	}
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
			continue;
		}
		char *sub = library_join(path, e->d_name);
		size += cloud_tree_size(sub);
		free(sub);
	}
	closedir(d);
	return size;
}

// ████████╗██╗   ██╗██████╗ ███████╗███████╗
// Thread safe: plain C only

static i32 cloud_type_ph(const char *category, i32 len) {
	static const char *names[] = {"Wood",    "Metal",       "Stone",           "Brick & Block",     "Concrete",
	                              "Ground & Terrain", "Asphalt & Bitumen", "Ceramic", "Textiles & Leather", "Plastic & Rubber",
	                              "Paper & Card", "Organic"};
	for (i32 i = 0; i < 12; ++i) {
		if ((i32)strlen(names[i]) == len && strncmp(category, names[i], len) == 0) {
			return i;
		}
	}
	return 13;
}

// The letters at the start of an ambientCG id name its type: PavingStones152
static i32 cloud_type_acg(const char *id) {
	static const struct {
		const char *prefix;
		i32         type;
	} table[] = {
	    {"Wood", 0},           {"WoodFloor", 0},      {"Planks", 0},          {"PaintedWood", 0},     {"Bark", 0},
	    {"WoodSiding", 0},     {"Chipboard", 0},      {"Fence", 0},           {"TreeEnd", 0},         {"WoodChips", 0},
	    {"Bamboo", 0},         {"Cork", 0},           {"Wicker", 0},          {"Logs", 0},            {"StickSet", 0},
	    {"Metal", 1},          {"DiamondPlate", 1},   {"MetalPlates", 1},     {"PaintedMetal", 1},    {"CorrugatedSteel", 1},
	    {"MetalWalkway", 1},   {"Rust", 1},           {"SheetMetal", 1},      {"Chainmail", 1},       {"Foil", 1},
	    {"Pipe", 1},           {"Grate", 1},          {"Rails", 1},           {"SolarPanel", 1},      {"PavingStones", 2},
	    {"Rock", 2},           {"Rocks", 2},          {"Marble", 2},          {"Granite", 2},         {"Onyx", 2},
	    {"Travertine", 2},     {"Bricks", 3},         {"PaintedBricks", 3},   {"Facade", 3},          {"TactilePaving", 3},
	    {"Concrete", 4},       {"Plaster", 4},        {"PaintedPlaster", 4},  {"OfficeCeiling", 4},   {"Ground", 5},
	    {"Gravel", 5},         {"Snow", 5},           {"Grass", 5},           {"Moss", 5},            {"ScatteredLeaves", 5},
	    {"Ice", 5},            {"Lava", 5},           {"Pathway", 5},         {"Footsteps", 5},       {"Asphalt", 6},
	    {"Road", 6},           {"Tiles", 7},          {"RoofingTiles", 7},    {"Terrazzo", 7},        {"Porcelain", 7},
	    {"GlazedTerracotta", 7}, {"Clay", 7},         {"Fabric", 8},          {"Leather", 8},         {"Carpet", 8},
	    {"Tatami", 8},         {"Net", 8},            {"Rope", 8},            {"Plastic", 9},         {"Rubber", 9},
	    {"Styrofoam", 9},      {"Foam", 9},           {"AcousticFoam", 9},    {"Sponge", 9},          {"Paper", 10},
	    {"Cardboard", 10},     {"Wallpaper", 10},     {"Painting", 10},       {"Leaf", 11},           {"ThatchedRoof", 11},
	    {"Pizza", 11},         {"Candy", 11},         {"Shells", 11},         {"FlowerSet", 11},      {"Ivory", 11},
	    {"SurfaceImperfections", 12}, {"Fingerprints", 12}, {"Smear", 12}, {"Scratches", 12},     {"Chip", 12},
	    {"Sign", 12},          {"Sticker", 12},       {"PaymentCard", 12},    {"Paint", 12},
	};
	i32 len = 0;
	while (id[len] != 0 && ((id[len] >= 'A' && id[len] <= 'Z') || (id[len] >= 'a' && id[len] <= 'z'))) {
		len++;
	}
	for (i32 i = 0; i < (i32)(sizeof(table) / sizeof(table[0])); ++i) {
		if ((i32)strlen(table[i].prefix) == len && strncmp(id, table[i].prefix, len) == 0) {
			return table[i].type;
		}
	}
	return 13;
}

// ██████╗ █████╗ ████████╗ █████╗ ██╗      ██████╗  ██████╗
// Parsed on a thread: plain C only

static i32 cloud_catalog_state = 0; // 0 - not loaded, 1 - fetching, 2 - parsing, 3 - ready, 4 - failed
static bool          cloud_catalog_refreshing = false; // Fetching a newer catalog while the cached one is shown
static char         *cloud_ph_json            = NULL;
static char        **cloud_acg_pages          = NULL;
static i32           cloud_acg_page_count     = 0;
static i32           cloud_acg_total          = 0;
static iron_thread_t cloud_parse_thread;
static volatile i32  cloud_parse_state  = 0; // 0 - idle, 1 - running, 2 - finished
static cloud_item_t **cloud_parsed      = NULL;
static i32            cloud_parsed_count = 0;
static i32            cloud_parsed_cap   = 0;

static i32 cloud_skip(jsmntok_t *t, i32 i) {
	i32 n = 1;
	while (n > 0) {
		n += t[i].size;
		n--;
		i++;
	}
	return i;
}

static bool cloud_tok_is(const char *js, jsmntok_t *t, const char *s) {
	i32 len = t->end - t->start;
	return t->type == JSMN_STRING && (i32)strlen(s) == len && strncmp(js + t->start, s, len) == 0;
}

// Copy of a json string, the escapes resolved
static char *cloud_tok_str(const char *js, jsmntok_t *t) {
	i32   len = t->end - t->start;
	char *out = malloc(len * 3 + 1);
	i32   o   = 0;
	for (i32 i = t->start; i < t->end; ++i) {
		char c = js[i];
		if (c != '\\' || i + 1 >= t->end) {
			out[o++] = c;
			continue;
		}
		char e = js[++i];
		if (e == 'u' && i + 4 < t->end) {
			char hex[5] = {js[i + 1], js[i + 2], js[i + 3], js[i + 4], 0};
			u32  cp     = (u32)strtol(hex, NULL, 16);
			i += 4;
			if (cp < 0x80) {
				out[o++] = (char)cp;
			}
			else if (cp < 0x800) {
				out[o++] = (char)(0xc0 | (cp >> 6));
				out[o++] = (char)(0x80 | (cp & 0x3f));
			}
			else {
				out[o++] = (char)(0xe0 | (cp >> 12));
				out[o++] = (char)(0x80 | ((cp >> 6) & 0x3f));
				out[o++] = (char)(0x80 | (cp & 0x3f));
			}
		}
		else {
			out[o++] = e == 'n' ? ' ' : e == 't' ? ' ' : e;
		}
	}
	out[o] = 0;
	return out;
}

static jsmntok_t *cloud_tokens(const char *js, i32 *count) {
	jsmn_parser p;
	jsmn_init(&p);
	i32 n = jsmn_parse(&p, js, strlen(js), NULL, 0);
	if (n <= 0) {
		*count = 0;
		return NULL;
	}
	jsmntok_t *t = malloc(sizeof(jsmntok_t) * n);
	jsmn_init(&p);
	if (jsmn_parse(&p, js, strlen(js), t, n) < 0) {
		free(t);
		*count = 0;
		return NULL;
	}
	*count = n;
	return t;
}

static void cloud_parsed_push(i32 source, char *id, char *name, char *search, i32 type) {
	if (cloud_parsed_count == cloud_parsed_cap) {
		cloud_parsed_cap = cloud_parsed_cap == 0 ? 1024 : cloud_parsed_cap * 2;
		cloud_parsed     = realloc(cloud_parsed, sizeof(cloud_item_t *) * cloud_parsed_cap);
	}
	cloud_item_t *it                   = calloc(1, sizeof(cloud_item_t));
	it->source                         = source;
	it->id                             = id;
	it->name                           = name;
	it->search                         = search;
	it->type                           = type;
	cloud_parsed[cloud_parsed_count++] = it;
}

static void cloud_lower(char *s) {
	for (; *s != 0; ++s) {
		*s = (char)tolower(*s);
	}
}

// Name, id and tags in one lowercase string for the search
static char *cloud_search_text(const char *js, jsmntok_t *t, i32 tags, const char *name, const char *id) {
	size_t cap = strlen(name) + strlen(id) + 4;
	if (tags >= 0) {
		i32 k = tags + 1;
		for (i32 j = 0; j < t[tags].size; ++j) {
			cap += t[k].end - t[k].start + 1;
			k = cloud_skip(t, k);
		}
	}
	char *s = malloc(cap);
	snprintf(s, cap, "%s %s", name, id);
	if (tags >= 0) {
		i32 k = tags + 1;
		for (i32 j = 0; j < t[tags].size; ++j) {
			if (t[k].type == JSMN_STRING) {
				strcat(s, " ");
				strncat(s, js + t[k].start, t[k].end - t[k].start);
			}
			k = cloud_skip(t, k);
		}
	}
	cloud_lower(s);
	return s;
}

static void cloud_parse_ph(const char *js) {
	i32        n = 0;
	jsmntok_t *t = cloud_tokens(js, &n);
	if (t == NULL || t[0].type != JSMN_OBJECT) {
		free(t);
		return;
	}
	i32 i = 1;
	for (i32 a = 0; a < t[0].size; ++a) {
		jsmntok_t *key = &t[i];
		jsmntok_t *obj = &t[i + 1];
		i32        o   = i + 1;
		i              = cloud_skip(t, i + 1);
		if (obj->type != JSMN_OBJECT) {
			continue;
		}
		char *name     = NULL;
		i32   type     = 13;
		i32   tags     = -1;
		i32   k        = o + 1;
		for (i32 f = 0; f < obj->size; ++f) {
			jsmntok_t *fk = &t[k];
			jsmntok_t *fv = &t[k + 1];
			if (cloud_tok_is(js, fk, "name") && fv->type == JSMN_STRING) {
				name = cloud_tok_str(js, fv);
			}
			else if (cloud_tok_is(js, fk, "category") && fv->type == JSMN_STRING) {
				const char *c     = js + fv->start;
				const char *slash = memchr(c, '/', fv->end - fv->start);
				type              = cloud_type_ph(c, slash != NULL ? (i32)(slash - c) : fv->end - fv->start);
			}
			else if (cloud_tok_is(js, fk, "tags") && fv->type == JSMN_ARRAY) {
				tags = k + 1;
			}
			k = cloud_skip(t, k + 1);
		}
		char *id = cloud_tok_str(js, key);
		if (name == NULL) {
			name = strdup(id);
		}
		cloud_parsed_push(CLOUD_PH, id, name, cloud_search_text(js, t, tags, name, id), type);
	}
	free(t);
}

static void cloud_parse_acg(const char *js) {
	i32        n = 0;
	jsmntok_t *t = cloud_tokens(js, &n);
	if (t == NULL || t[0].type != JSMN_OBJECT) {
		free(t);
		return;
	}
	i32 i = 1;
	for (i32 a = 0; a < t[0].size; ++a) {
		bool assets = cloud_tok_is(js, &t[i], "assets") && t[i + 1].type == JSMN_ARRAY;
		if (!assets) {
			i = cloud_skip(t, i + 1);
			continue;
		}
		i32 count = t[i + 1].size;
		i32 k     = i + 2;
		for (i32 e = 0; e < count; ++e) {
			jsmntok_t *obj  = &t[k];
			i32        next = cloud_skip(t, k);
			char      *id   = NULL;
			char      *name = NULL;
			i32        tags = -1;
			i32        f    = k + 1;
			for (i32 j = 0; obj->type == JSMN_OBJECT && j < obj->size; ++j) {
				jsmntok_t *fk = &t[f];
				jsmntok_t *fv = &t[f + 1];
				if (cloud_tok_is(js, fk, "id") && fv->type == JSMN_STRING) {
					id = cloud_tok_str(js, fv);
				}
				else if (cloud_tok_is(js, fk, "title") && fv->type == JSMN_STRING) {
					name = cloud_tok_str(js, fv);
				}
				else if (cloud_tok_is(js, fk, "tags") && fv->type == JSMN_ARRAY) {
					tags = f + 1;
				}
				f = cloud_skip(t, f + 1);
			}
			if (id != NULL) {
				if (name == NULL) {
					name = strdup(id);
				}
				cloud_parsed_push(CLOUD_ACG, id, name, cloud_search_text(js, t, tags, name, id), cloud_type_acg(id));
			}
			else {
				free(name);
			}
			k = next;
		}
		break;
	}
	free(t);
}

static i32 cloud_compare(const void *a, const void *b) {
	cloud_item_t *x = *(cloud_item_t **)a;
	cloud_item_t *y = *(cloud_item_t **)b;
	if (x->type != y->type) {
		return x->type - y->type;
	}
	return strcasecmp(x->name, y->name);
}

static void cloud_parse_run(void *_) {
	if (cloud_ph_json != NULL) {
		cloud_parse_ph(cloud_ph_json);
	}
	for (i32 i = 0; i < cloud_acg_page_count; ++i) {
		cloud_parse_acg(cloud_acg_pages[i]);
	}
	qsort(cloud_parsed, cloud_parsed_count, sizeof(cloud_item_t *), cloud_compare);
	cloud_parse_state = 2;
}

static void cloud_item_free(cloud_item_t *it) {
	free(it->id);
	free(it->name);
	free(it->search);
	if (it->icon != NULL) {
		gpu_delete_texture(it->icon);
	}
	free(it);
}

static char *cloud_catalog_path() {
	return string("%scloud_catalog.txt", iron_internal_save_path());
}

static char *cloud_material_dir(cloud_item_t *it, i32 res) {
	return string("%s/%s/%s_%s", cloud_dir(), it->source == CLOUD_PH ? "ph" : "acg", it->id, cloud_res_names[res]);
}

static void cloud_update_downloaded() {
	for (i32 i = 0; i < cloud_count; ++i) {
		cloud_item_t *it = cloud_items[i];
		it->downloaded   = iron_file_exists(string("%s/.complete", cloud_material_dir(it, g_config->cloud_resolution)));
	}
}

// One line per item: source, id, type, name, search text
static void cloud_save_catalog() {
	FILE *f = fopen(cloud_catalog_path(), "wb");
	if (f == NULL) {
		return;
	}
	fprintf(f, "v1\t%lld\n", (long long)time(NULL));
	for (i32 i = 0; i < cloud_count; ++i) {
		cloud_item_t *it = cloud_items[i];
		fprintf(f, "%d\t%s\t%d\t%s\t%s\n", it->source, it->id, it->type, it->name, it->search);
	}
	fclose(f);
}

// Returns the age of the cached catalog in seconds, -1 when there is none
static i64 cloud_load_catalog() {
	FILE *f = fopen(cloud_catalog_path(), "rb");
	if (f == NULL) {
		return -1;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *text = malloc(size + 1);
	size_t r   = fread(text, 1, size, f);
	fclose(f);
	text[r]     = 0;
	i64   stamp = 0;
	char *line  = text;
	bool  first = true;
	while (*line != 0) {
		char *end = strchr(line, '\n');
		if (end != NULL) {
			*end = 0;
		}
		if (first) {
			first = false;
			if (strncmp(line, "v1\t", 3) == 0) {
				stamp = atoll(line + 3);
			}
		}
		else {
			char *fields[5] = {0};
			i32   n         = 0;
			char *c         = line;
			while (n < 5) {
				fields[n++] = c;
				char *tab   = strchr(c, '\t');
				if (tab == NULL) {
					break;
				}
				*tab = 0;
				c    = tab + 1;
			}
			if (n == 5) {
				cloud_parsed_push(atoi(fields[0]), strdup(fields[1]), strdup(fields[3]), strdup(fields[4]), atoi(fields[2]));
			}
		}
		if (end == NULL) {
			break;
		}
		line = end + 1;
	}
	free(text);
	return stamp > 0 ? (i64)time(NULL) - stamp : CLOUD_CATALOG_MAX_AGE + 1;
}

// The parsed items replace the shown ones
static void cloud_adopt_parsed() {
	// Keep the loaded thumbnails
	for (i32 i = 0; i < cloud_parsed_count; ++i) {
		cloud_item_t *p = cloud_parsed[i];
		for (i32 j = 0; j < cloud_count; ++j) {
			cloud_item_t *o = cloud_items[j];
			if (o != NULL && o->source == p->source && strcmp(o->id, p->id) == 0) {
				if (o->icon_state == 4 || o->icon_state == 2 || o->icon_state == 5) {
					p->icon       = o->icon;
					p->icon_state = o->icon_state;
					o->icon       = NULL;
				}
				p->busy = o->busy;
				break;
			}
		}
	}
	for (i32 j = 0; j < cloud_count; ++j) {
		cloud_item_free(cloud_items[j]);
	}
	free(cloud_items);
	cloud_items        = cloud_parsed;
	cloud_count        = cloud_parsed_count;
	cloud_parsed       = NULL;
	cloud_parsed_count = 0;
	cloud_parsed_cap   = 0;
	cloud_update_downloaded();
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
}

static void cloud_fetch_acg_page();

static void cloud_fetch_failed() {
	if (!cloud_catalog_refreshing) {
		cloud_catalog_state = 4;
	}
	cloud_catalog_refreshing = false;
	console_error(tr("Could not load the cloud materials, check the internet connection"));
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
}

static void cloud_fetch_done() {
	if (cloud_acg_total == 0 && cloud_ph_json == NULL) {
		cloud_fetch_failed();
		return;
	}
	if (!cloud_catalog_refreshing) {
		cloud_catalog_state = 2;
	}
	cloud_parse_state = 1;
	iron_thread_init(&cloud_parse_thread, cloud_parse_run, NULL);
}

static void cloud_on_acg_page(int status, const char *body, void *data) {
	if (status != 200 || body == NULL) {
		cloud_free_body(body);
		cloud_fetch_done(); // Keep what was fetched so far
		return;
	}
	cloud_acg_pages                         = realloc(cloud_acg_pages, sizeof(char *) * (cloud_acg_page_count + 1));
	cloud_acg_pages[cloud_acg_page_count++] = strdup(body);
	const char *total                       = strstr(body, "\"totalResults\"");
	if (total != NULL) {
		cloud_acg_total = atoi(strchr(total, ':') + 1);
	}
	bool more = strstr(body, "\"id\"") != NULL && cloud_acg_page_count * 100 < cloud_acg_total;
	cloud_free_body(body);
	more ? cloud_fetch_acg_page() : cloud_fetch_done();
}

static void cloud_fetch_acg_page() {
	char *url = string("https://ambientcg.com/api/v3/assets?type=material&sort=alphabet&limit=100&offset=%d&include=title,tags", cloud_acg_page_count * 100);
	cloud_request(url, NULL, cloud_on_acg_page, NULL);
}

static void cloud_on_ph(int status, const char *body, void *data) {
	if (status == 200 && body != NULL) {
		cloud_ph_json = strdup(body);
	}
	cloud_free_body(body);
	cloud_fetch_acg_page();
}

static void cloud_fetch_catalog() {
	if (cloud_parse_state != 0 || cloud_catalog_state == 1 || cloud_catalog_refreshing) {
		return;
	}
	free(cloud_ph_json);
	cloud_ph_json = NULL;
	for (i32 i = 0; i < cloud_acg_page_count; ++i) {
		free(cloud_acg_pages[i]);
	}
	free(cloud_acg_pages);
	cloud_acg_pages      = NULL;
	cloud_acg_page_count = 0;
	cloud_acg_total      = 0;
	if (cloud_catalog_state == 3) {
		cloud_catalog_refreshing = true; // The current list stays until the new one is parsed
	}
	else {
		cloud_catalog_state = 1;
	}
	cloud_request("https://api.polyhaven.com/assets?t=textures", NULL, cloud_on_ph, NULL);
}

// ████████╗██╗  ██╗██╗   ██╗███╗   ███╗██████╗ ███████╗

static i32              cloud_thumb_downloads = 0;
static iron_thread_t    cloud_icon_thread;
static volatile i32     cloud_icon_state  = 0; // 0 - idle, 1 - decoding, 2 - finished
static cloud_item_t    *cloud_icon_item   = NULL;
static char            *cloud_icon_path   = NULL;
static iron_image_pixels_t cloud_icon_pixels = {0};
static i32              cloud_icons         = 0;

static char *cloud_thumb_path(cloud_item_t *it) {
	return string("%s/thumbs/%s_%s.png", cloud_dir(), it->source == CLOUD_PH ? "ph" : "acg", it->id);
}

static char *cloud_thumb_url(cloud_item_t *it) {
	if (it->source == CLOUD_PH) {
		return string("https://cdn.polyhaven.com/asset_img/thumbs/%s.png?width=128&height=128&format=png", it->id);
	}
	return string("https://acg-media.struffelproductions.com/file/ambientCG-Web/media/thumbnail/128-PNG/%s.png", it->id);
}

static void cloud_on_thumb(int status, const char *body, void *data) {
	cloud_free_body(body);
	cloud_thumb_downloads--;
	// The item is looked up again, the list may have been replaced meanwhile. data is "<source>_<id>"
	char *id = data;
	for (i32 i = 0; i < cloud_count; ++i) {
		cloud_item_t *it = cloud_items[i];
		if (it->icon_state == 1 && it->source == id[0] - '0' && strcmp(it->id, id + 2) == 0) {
			it->icon_state = status == 200 && cloud_is_image(cloud_thumb_path(it)) ? 2 : 5;
			break;
		}
	}
	free(id);
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
}

static void cloud_icon_run(void *_) {
	if (!library_decode(cloud_icon_path, &cloud_icon_pixels, LIBRARY_ICON_RES)) {
		cloud_icon_pixels.data = NULL;
	}
	cloud_icon_state = 2;
}

// Asks for the thumbnail of an item that is drawn, returns it once loaded
gpu_texture_t *library_cloud_item_icon(cloud_item_t *it) {
	it->last_seen = g_context->frame;
	if (it->icon_state == 0) {
		if (iron_file_exists(cloud_thumb_path(it))) {
			it->icon_state = 2;
		}
		else if (cloud_thumb_downloads < CLOUD_MAX_THUMB_DOWNLOADS) {
			iron_create_directory(string("%s/thumbs", cloud_dir()));
			it->icon_state = 1;
			cloud_thumb_downloads++;
			cloud_request(cloud_thumb_url(it), cloud_thumb_path(it), cloud_on_thumb, strdup(string("%d_%s", it->source, it->id)));
		}
	}
	if (it->icon_state == 2 && cloud_icon_state == 0) {
		it->icon_state  = 3;
		cloud_icon_item = it;
		free(cloud_icon_path);
		cloud_icon_path  = strdup(cloud_thumb_path(it));
		cloud_icon_state = 1;
		iron_thread_init(&cloud_icon_thread, cloud_icon_run, NULL);
	}
	return it->icon_state == 4 ? it->icon : NULL;
}

static void cloud_update_icons() {
	if (cloud_icon_state == 2) {
		iron_thread_wait_and_destroy(&cloud_icon_thread);
		cloud_item_t *it = cloud_icon_item;
		if (it != NULL && cloud_icon_pixels.data != NULL) {
			it->icon       = gpu_create_texture_from_pixels(&cloud_icon_pixels);
			it->icon_state = 4;
			cloud_icons++;
		}
		else if (it != NULL) {
			it->icon_state = 5;
			remove(cloud_icon_path); // Not an image, downloaded again next time
		}
		cloud_icon_item  = NULL;
		cloud_icon_state = 0;
		ui_dock_redraw(TAB_AREA_SIDEBAR1);
	}

	while (cloud_icons > CLOUD_MAX_ICONS) {
		cloud_item_t *oldest = NULL;
		for (i32 i = 0; i < cloud_count; ++i) {
			cloud_item_t *it = cloud_items[i];
			if (it->icon != NULL && (oldest == NULL || it->last_seen < oldest->last_seen)) {
				oldest = it;
			}
		}
		if (oldest == NULL) {
			break;
		}
		gpu_delete_texture(oldest->icon);
		oldest->icon       = NULL;
		oldest->icon_state = 0;
		cloud_icons--;
	}
}

// ██████╗  ██████╗ ██╗    ██╗███╗   ██╗██╗      ██████╗  █████╗ ██████╗

typedef struct cloud_job {
	cloud_item_t *item;
	char         *name;
	i32           res;
	char         *dir;
	// Poly Haven: maps left to download
	char **urls;
	char **files;
	f64   *sizes;
	f64    total;     // Bytes of all the files
	f64    done;      // Bytes of the files downloaded already
	i32    file_count;
	i32    file_pos;
} cloud_job_t;

static cloud_job_t   *cloud_job        = NULL;
static cloud_item_t **cloud_queue      = NULL;
static i32            cloud_queue_len  = 0;
static i32            cloud_queue_pos  = 0;
static iron_thread_t  cloud_zip_thread;
static volatile i32   cloud_zip_state  = 0; // 0 - idle, 1 - extracting, 2 - finished
static volatile bool  cloud_zip_ok     = false;
static f64            cloud_disk_usage = -1;

static void cloud_start_next();

static void cloud_job_free(cloud_job_t *job) {
	free(job->name);
	free(job->dir);
	for (i32 i = 0; i < job->file_count; ++i) {
		free(job->urls[i]);
		free(job->files[i]);
	}
	free(job->urls);
	free(job->files);
	free(job->sizes);
	free(job);
}

typedef struct cloud_import {
	char *dir;
	char *name;
} cloud_import_t;

// Next frame: the import renders the material preview, it can not run while the Library tab draws
static void cloud_import_on_next_frame(void *data) {
	cloud_import_t *imp = data;
	import_folder_run_named(imp->dir, imp->name); // The strings may be kept by the material, they are not freed
	free(imp);
}

static void cloud_job_end(bool ok) {
	cloud_job_t *job = cloud_job;
	cloud_job        = NULL;
	job->item->busy  = false;
	if (ok) {
		FILE *f = fopen(string("%s/.complete", job->dir), "wb");
		if (f != NULL) {
			fclose(f);
		}
		job->item->downloaded = job->res == g_config->cloud_resolution;
		cloud_disk_usage      = -1;
		cloud_import_t *imp   = malloc(sizeof(cloud_import_t));
		imp->dir              = string_copy(job->dir);
		imp->name             = string_copy(job->name);
		sys_notify_on_next_frame(cloud_import_on_next_frame, imp);
	}
	else {
		cloud_remove_tree(job->dir);
		any_map_t *vars = any_map_create();
		any_map_set(vars, "name", job->name);
		console_error(vtr("Could not download {name}", vars));
		map_free(vars);
	}
	cloud_job_free(job);
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
	cloud_start_next();
}

// Poly Haven: one map after the other

static void cloud_ph_next_file();

static void cloud_on_ph_file(int status, const char *body, void *data) {
	cloud_free_body(body);
	cloud_job_t *job  = cloud_job;
	char        *path = library_join(job->dir, job->files[job->file_pos]);
	bool         ok   = status == 200 && cloud_is_image(path);
	free(path);
	if (!ok) {
		cloud_job_end(false);
		return;
	}
	job->done += job->sizes[job->file_pos];
	job->file_pos++;
	cloud_ph_next_file();
}

static void cloud_ph_next_file() {
	cloud_job_t *job = cloud_job;
	if (job->file_pos >= job->file_count) {
		cloud_job_end(job->file_count > 0);
		return;
	}
	char *path = library_join(job->dir, job->files[job->file_pos]);
	cloud_request(job->urls[job->file_pos], path, cloud_on_ph_file, NULL);
	free(path);
}

// Poly Haven map names, the rest (nor_dx, arm, gltf...) is skipped
static const char *cloud_ph_tag(const char *map) {
	static const char *pairs[][2] = {{"diffuse", "basecolor"},    {"nor_gl", "normal"},   {"ao", "ao"},           {"rough", "roughness"},
	                                 {"metal", "metallic"},       {"displacement", "height"}, {"alpha", "opacity"}, {"mask", "opacity"},
	                                 {"opacity", "opacity"}};
	for (i32 i = 0; i < (i32)(sizeof(pairs) / sizeof(pairs[0])); ++i) {
		if (strcasecmp(map, pairs[i][0]) == 0) {
			return pairs[i][1];
		}
	}
	return NULL;
}

static void cloud_job_add_file(cloud_job_t *job, char *url, char *file, f64 size) {
	for (i32 i = 0; i < job->file_count; ++i) {
		if (strcmp(job->files[i], file) == 0) {
			free(url);
			free(file);
			return;
		}
	}
	job->urls                    = realloc(job->urls, sizeof(char *) * (job->file_count + 1));
	job->files                   = realloc(job->files, sizeof(char *) * (job->file_count + 1));
	job->sizes                   = realloc(job->sizes, sizeof(f64) * (job->file_count + 1));
	job->sizes[job->file_count]  = size;
	job->total += size;
	job->urls[job->file_count]   = url;
	job->files[job->file_count]  = file;
	job->file_count++;
}

// Files list: {map: {"1k": {"jpg": {"url": ...}}}}, the wanted size or the closest smaller one
static void cloud_on_ph_files(int status, const char *body, void *data) {
	cloud_job_t *job = cloud_job;
	if (status != 200 || body == NULL) {
		cloud_free_body(body);
		cloud_job_end(false);
		return;
	}
	char      *js = strdup(body);
	cloud_free_body(body);
	i32        n = 0;
	jsmntok_t *t = cloud_tokens(js, &n);
	if (t != NULL && t[0].type == JSMN_OBJECT) {
		i32 i = 1;
		for (i32 m = 0; m < t[0].size; ++m) {
			char       *map  = cloud_tok_str(js, &t[i]);
			const char *tag  = cloud_ph_tag(map);
			i32         obj  = i + 1;
			i               = cloud_skip(t, i + 1);
			if (tag == NULL || t[obj].type != JSMN_OBJECT) {
				free(map);
				continue;
			}
			char *best      = NULL;
			char *best_ext  = NULL;
			f64   best_size = 0;
			i32   best_res = -1;
			i32   k        = obj + 1;
			for (i32 r = 0; r < t[obj].size; ++r) {
				char *res_name = cloud_tok_str(js, &t[k]);
				i32   res      = -1;
				for (i32 x = 0; x < 4; ++x) {
					if (strcasecmp(res_name, cloud_res_names[x]) == 0) {
						res = x;
					}
				}
				free(res_name);
				i32 formats = k + 1;
				k           = cloud_skip(t, k + 1);
				if (res < 0 || res > job->res || res <= best_res || t[formats].type != JSMN_OBJECT) {
					continue;
				}
				// jpg, else png
				i32 f = formats + 1;
				for (i32 x = 0; x < t[formats].size; ++x) {
					bool jpg = cloud_tok_is(js, &t[f], "jpg");
					bool png = cloud_tok_is(js, &t[f], "png");
					i32  fo  = f + 1;
					f        = cloud_skip(t, f + 1);
					if ((!jpg && !png) || (png && best_res == res) || t[fo].type != JSMN_OBJECT) {
						continue;
					}
					i32 u = fo + 1;
					for (i32 y = 0; y < t[fo].size; ++y) {
						if (cloud_tok_is(js, &t[u], "url")) {
							free(best);
							best     = cloud_tok_str(js, &t[u + 1]);
							best_ext = jpg ? "jpg" : "png";
							best_res = res;
						}
						else if (cloud_tok_is(js, &t[u], "size")) {
							best_size = atof(js + t[u + 1].start);
						}
						u = cloud_skip(t, u + 1);
					}
				}
			}
			if (best != NULL) {
				size_t len  = strlen(job->item->id) + strlen(tag) + 8;
				char  *file = malloc(len);
				snprintf(file, len, "%s_%s.%s", job->item->id, tag, best_ext);
				cloud_job_add_file(job, best, file, best_size);
			}
			free(map);
		}
	}
	free(t);
	free(js);
	cloud_ph_next_file();
}

// ambientCG: one zip with all the maps

static const char *cloud_acg_tag(const char *file) {
	static const char *pairs[][2] = {{"Color", "basecolor"},       {"NormalGL", "normal"},       {"AmbientOcclusion", "ao"},
	                                 {"Roughness", "roughness"},   {"Metalness", "metallic"},    {"Displacement", "height"},
	                                 {"Opacity", "opacity"}};
	const char *dot = strrchr(file, '.');
	const char *sep = strrchr(file, '_');
	if (dot == NULL || sep == NULL || sep > dot) {
		return NULL;
	}
	i32 len = (i32)(dot - sep - 1);
	for (i32 i = 0; i < (i32)(sizeof(pairs) / sizeof(pairs[0])); ++i) {
		if ((i32)strlen(pairs[i][0]) == len && strncasecmp(sep + 1, pairs[i][0], len) == 0) {
			return pairs[i][1];
		}
	}
	return NULL;
}

static u32 cloud_u16(const unsigned char *p) {
	return p[0] | (p[1] << 8);
}

static u32 cloud_u32(const unsigned char *p) {
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24);
}

// Extracts the known maps of the zip through its central directory, stored or deflate entries
static bool cloud_unzip(const char *zip_path, const char *dir, const char *id) {
	FILE *f = fopen(zip_path, "rb");
	if (f == NULL) {
		return false;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	// End of central directory, within the last 64 KB
	long          tail_len = size < 65557 ? size : 65557;
	unsigned char *tail    = malloc(tail_len);
	fseek(f, size - tail_len, SEEK_SET);
	fread(tail, 1, tail_len, f);
	long eocd = -1;
	for (long i = tail_len - 22; i >= 0; --i) {
		if (cloud_u32(tail + i) == 0x06054b50) {
			eocd = i;
			break;
		}
	}
	if (eocd < 0) {
		free(tail);
		fclose(f);
		return false;
	}
	u32 entries = cloud_u16(tail + eocd + 10);
	u32 cd_size = cloud_u32(tail + eocd + 12);
	u32 cd_off  = cloud_u32(tail + eocd + 16);
	free(tail);
	unsigned char *cd = malloc(cd_size);
	fseek(f, cd_off, SEEK_SET);
	if (fread(cd, 1, cd_size, f) != cd_size) {
		free(cd);
		fclose(f);
		return false;
	}
	i32 written = 0;
	u32 p       = 0;
	for (u32 e = 0; e < entries && p + 46 <= cd_size; ++e) {
		if (cloud_u32(cd + p) != 0x02014b50) {
			break;
		}
		u32  method = cloud_u16(cd + p + 10);
		u32  csize  = cloud_u32(cd + p + 20);
		u32  usize  = cloud_u32(cd + p + 24);
		u32  nlen   = cloud_u16(cd + p + 28);
		u32  xlen   = cloud_u16(cd + p + 30);
		u32  clen   = cloud_u16(cd + p + 32);
		u32  local  = cloud_u32(cd + p + 42);
		char name[512];
		u32  copy = nlen < sizeof(name) - 1 ? nlen : sizeof(name) - 1;
		memcpy(name, cd + p + 46, copy);
		name[copy] = 0;
		p += 46 + nlen + xlen + clen;

		const char *base = strrchr(name, '/');
		base             = base != NULL ? base + 1 : name;
		const char *tag  = cloud_acg_tag(base);
		const char *ext  = strrchr(base, '.');
		if (tag == NULL || ext == NULL || (strcasecmp(ext, ".jpg") != 0 && strcasecmp(ext, ".png") != 0) || (method != 0 && method != 8)) {
			continue;
		}
		unsigned char lh[30];
		fseek(f, local, SEEK_SET);
		if (fread(lh, 1, 30, f) != 30 || cloud_u32(lh) != 0x04034b50) {
			continue;
		}
		fseek(f, local + 30 + cloud_u16(lh + 26) + cloud_u16(lh + 28), SEEK_SET);
		unsigned char *in = malloc(csize > 0 ? csize : 1);
		if (fread(in, 1, csize, f) != csize) {
			free(in);
			continue;
		}
		unsigned char *out = in;
		if (method == 8) {
			out = malloc(usize > 0 ? usize : 1);
			if (sinflate(out, usize, in, csize) != (int)usize) {
				free(out);
				free(in);
				continue;
			}
			free(in);
		}
		char path[1024];
		snprintf(path, sizeof(path), "%s/%s_%s%s", dir, id, tag, ext);
		FILE *o = fopen(path, "wb");
		if (o != NULL) {
			fwrite(out, 1, usize, o);
			fclose(o);
			written++;
		}
		free(out);
	}
	free(cd);
	fclose(f);
	return written > 0;
}

static char *cloud_zip_path   = NULL;
static char *cloud_zip_dir    = NULL;
static char *cloud_zip_id     = NULL;

static void cloud_zip_run(void *_) {
	cloud_zip_ok = cloud_unzip(cloud_zip_path, cloud_zip_dir, cloud_zip_id);
	remove(cloud_zip_path);
	cloud_zip_state = 2;
}

static void cloud_acg_download();

static void cloud_on_acg_zip(int status, const char *body, void *data) {
	cloud_free_body(body);
	cloud_job_t *job = cloud_job;
	char        *zip = string("%s/download.zip", job->dir);
	static const unsigned char magic[] = {'P', 'K', 3, 4};
	if (status != 200 || !cloud_file_starts_with(zip, magic, 4)) {
		remove(zip);
		// Not available in this size, try the next smaller one
		if (job->res > 0) {
			cloud_remove_tree(job->dir);
			job->res--;
			free(job->dir);
			job->dir = strdup(cloud_material_dir(job->item, job->res));
			cloud_acg_download();
			return;
		}
		cloud_job_end(false);
		return;
	}
	free(cloud_zip_path);
	free(cloud_zip_dir);
	free(cloud_zip_id);
	cloud_zip_path  = strdup(zip);
	cloud_zip_dir   = strdup(job->dir);
	cloud_zip_id    = strdup(job->item->id);
	cloud_zip_state = 1;
	iron_thread_init(&cloud_zip_thread, cloud_zip_run, NULL);
}

static void cloud_acg_download() {
	cloud_job_t *job = cloud_job;
	if (iron_file_exists(string("%s/.complete", job->dir))) {
		cloud_job_end(true);
		return;
	}
	iron_create_directory(job->dir);
	char *url = string("https://ambientcg.com/get?file=%s_%s-JPG.zip", job->item->id, cloud_res_names[job->res]);
	cloud_request(url, string("%s/download.zip", job->dir), cloud_on_acg_zip, NULL);
}

static void cloud_start_next() {
	if (cloud_job != NULL) {
		return;
	}
	cloud_item_t *it = NULL;
	while (cloud_queue_pos < cloud_queue_len && it == NULL) {
		it = cloud_queue[cloud_queue_pos++];
	}
	if (it == NULL) {
		cloud_queue_pos = 0;
		cloud_queue_len = 0;
		return;
	}
	cloud_job_t *job = calloc(1, sizeof(cloud_job_t));
	job->item        = it;
	job->name        = strdup(it->name);
	job->res         = g_config->cloud_resolution;
	job->dir         = strdup(cloud_material_dir(it, job->res));
	cloud_job        = job;

	if (iron_file_exists(string("%s/.complete", job->dir))) {
		cloud_job_end(true);
		return;
	}
	if (it->source == CLOUD_PH) {
		iron_create_directory(job->dir);
		cloud_request(string("https://api.polyhaven.com/files/%s", it->id), NULL, cloud_on_ph_files, NULL);
	}
	else {
		cloud_acg_download();
	}
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
}

// ██╗   ██╗██████╗ ██████╗  █████╗ ████████╗███████╗

static void cloud_update(void *_) {
	// The new list replaces the items once no download or thumbnail decode points to them
	if (cloud_parse_state == 2 && cloud_job == NULL && cloud_queue_len == 0 && cloud_icon_state == 0) {
		iron_thread_wait_and_destroy(&cloud_parse_thread);
		cloud_parse_state = 0;
		bool refreshed    = cloud_catalog_refreshing || cloud_catalog_state == 2;
		cloud_catalog_refreshing = false;
		if (cloud_parsed_count > 0) {
			cloud_adopt_parsed();
			cloud_catalog_state = 3;
			if (refreshed) {
				cloud_save_catalog();
			}
		}
		else if (cloud_catalog_state != 3) {
			cloud_catalog_state = 4;
		}
		ui_dock_redraw(TAB_AREA_SIDEBAR1);
	}
	if (cloud_zip_state == 2) {
		iron_thread_wait_and_destroy(&cloud_zip_thread);
		cloud_zip_state = 0;
		cloud_job_end(cloud_zip_ok);
	}
	cloud_update_icons();
	if (cloud_job != NULL) {
		ui_dock_redraw(TAB_AREA_SIDEBAR1); // Download progress
	}
}

// ██╗   ██╗██╗

// Called when the cloud view is shown: the cached catalog right away, a fresh one when missing or old
void library_cloud_open() {
	static bool update_added = false;
	if (!update_added) {
		update_added = true;
		sys_notify_on_update(cloud_update, NULL);
	}
	if (cloud_catalog_state != 0) {
		return;
	}
	i64 age = cloud_load_catalog();
	if (cloud_parsed_count > 0) {
		cloud_adopt_parsed();
		cloud_catalog_state = 3;
	}
	if (age < 0 || age > CLOUD_CATALOG_MAX_AGE || cloud_count == 0) {
		cloud_fetch_catalog();
	}
}

void library_cloud_refresh() {
	if (cloud_catalog_state == 0) {
		library_cloud_open();
		return;
	}
	if (cloud_catalog_state == 4) {
		cloud_catalog_state = 0;
	}
	cloud_fetch_catalog();
}

i32 library_cloud_count() {
	return cloud_count;
}

cloud_item_t *library_cloud_get(i32 i) {
	return cloud_items[i];
}

char *library_cloud_item_name(cloud_item_t *it) {
	return it->name;
}

i32 library_cloud_item_type(cloud_item_t *it) {
	return it->type;
}

bool library_cloud_item_busy(cloud_item_t *it) {
	return it->busy;
}

bool library_cloud_item_downloaded(cloud_item_t *it) {
	return it->downloaded;
}

bool library_cloud_item_matches(cloud_item_t *it, char *search) {
	return strstr(it->search, search) != NULL;
}

char *library_cloud_item_source(cloud_item_t *it) {
	return it->source == CLOUD_PH ? "Poly Haven" : "ambientCG";
}

i32 library_cloud_type_count() {
	return CLOUD_TYPES;
}

char *library_cloud_type_name(i32 type) {
	return tr(cloud_type_names[type]);
}

char *library_cloud_res_name(i32 res) {
	return cloud_res_names[res];
}

// Adds the material to the project: selected when it is there already, downloaded first when needed
void library_cloud_use(cloud_item_t *it) {
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		if (string_equals(g_project->_->materials->buffer[i]->canvas->name, it->name)) {
			context_select_material(i);
			return;
		}
	}
	if (it->busy) {
		return;
	}
	it->busy = true;
	cloud_queue                    = realloc(cloud_queue, sizeof(cloud_item_t *) * (cloud_queue_len + 1));
	cloud_queue[cloud_queue_len++] = it;
	cloud_start_next();
}

// Shown while the catalog loads or when it could not be loaded, "" otherwise
char *library_cloud_status() {
	if (cloud_catalog_state == 1 || cloud_catalog_state == 2) {
		return tr("Loading the cloud materials...");
	}
	if (cloud_catalog_state == 4) {
		return tr("Could not load the cloud materials");
	}
	return "";
}

// Download progress of an item from 0 to 1, 0 while it waits for its turn
f32 library_cloud_item_progress(cloud_item_t *it) {
	cloud_job_t *job = cloud_job;
	if (job == NULL || job->item != it) {
		return 0.0;
	}
	if (cloud_zip_state != 0) {
		return 1.0; // Extracting
	}
	u64 written  = 0;
	u64 expected = 0;
	if (it->source == CLOUD_ACG) {
		return iron_net_progress(string("%s/download.zip", job->dir), &written, &expected) ? (f32)written / expected : 0.0;
	}
	if (job->total <= 0 || job->file_pos >= job->file_count) {
		return 0.0;
	}
	f64 current = 0;
	if (iron_net_progress(string("%s/%s", job->dir, job->files[job->file_pos]), &written, &expected)) {
		current = (f64)written;
	}
	f32 p = (f32)((job->done + current) / job->total);
	return p > 1.0 ? 1.0 : p;
}

// Size of the downloaded textures, thumbnails included
f64 library_cloud_disk_usage() {
	if (cloud_disk_usage < 0) {
		cloud_disk_usage = cloud_tree_size(cloud_dir());
	}
	return cloud_disk_usage;
}

void library_cloud_clear_downloads() {
	if (cloud_job != NULL || cloud_zip_state != 0) {
		console_info(tr("Wait for the current download to finish"));
		return;
	}
	cloud_remove_tree(string("%s/ph", cloud_dir()));
	cloud_remove_tree(string("%s/acg", cloud_dir()));
	cloud_disk_usage = -1;
	cloud_update_downloaded();
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
}

// Texture size changed in the preferences
void library_cloud_resolution_changed() {
	cloud_update_downloaded();
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
}
