/**
 * @file font_manager.c
 * @brief Cross-platform font discovery and management implementation
 */

#define _GNU_SOURCE

#include <sys/stat.h>

#include <ctype.h>
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "font_manager.h"

/* Font cache entry */
typedef struct font_cache_entry_t {
	char *family;
	char *path;
	img2_font_weight_t weight;
	img2_font_style_t style;
	uint8_t *data;
	size_t data_size;
	struct img2_font *handle; /* [R4] interned handle, built on first lookup; owned by the entry */
	struct font_cache_entry_t *next;
} font_cache_entry_t;

/* Font structure */
struct img2_font {
	char *family;
	img2_font_weight_t weight;
	img2_font_style_t style;
	const uint8_t *data;
	size_t data_size;
};

/* Global font cache: every g_* below is guarded by s_font_lock ([M1a]: mutex + flag, so re-init works) */
static pthread_mutex_t s_font_lock = PTHREAD_MUTEX_INITIALIZER;
static font_cache_entry_t *g_font_cache = NULL;
static img2_font_t *g_fallback_font = NULL;
static bool g_initialized = false;

/* Embedded fallback fonts */
#include "embedded_fonts.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC /* TU-local: nanosvg reaches glyph data via the img2_font_face_* wrappers */
#define STBTT_assert(x) ((void)0)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb_truetype.h"
#pragma GCC diagnostic pop
#include "../core/log.h"

/* Parsed font face behind the img2_font_face_* wrappers */
struct img2_font_face {
	stbtt_fontinfo info;
};

/**
 * @brief Platform-specific font search paths
 */
static const char *get_system_font_paths(int index)
{
#if defined(__linux__)
	static const char *const linux_paths[] = { "/usr/share/fonts", "/usr/local/share/fonts", "~/.local/share/fonts", "~/.fonts", NULL };
	return linux_paths[index];

#elif defined(__APPLE__)
	static const char *const macos_paths[] = { "/System/Library/Fonts", "/Library/Fonts", "~/Library/Fonts", NULL };
	return macos_paths[index];

#elif defined(_WIN32)
	static const char *const windows_paths[] = { "C:\\Windows\\Fonts", NULL };
	return windows_paths[index];

#else
	return NULL;
#endif
}

/**
 * @brief Expand ~ to home directory
 */
static char *expand_path(const char *path)
{
	if (path[0] != '~') {
		return strdup(path);
	}

#ifdef IMG2_HAVE_SECURE_GETENV
	const char *home = secure_getenv("HOME"); /* [SL1] ignore HOME in setuid/setgid processes */
#else
	const char *home = getenv("HOME");
#endif
	if (home == NULL) {
		return strdup(path);
	}

	size_t len = strlen(home) + strlen(path);
	char *expanded = (char *)malloc(len);
	if (expanded == NULL) {
		return NULL;
	}

	snprintf(expanded, len, "%s%s", home, path + 1);
	return expanded;
}

/**
 * @brief Check if a file is a TrueType font
 */
static bool is_ttf_file(const char *filename)
{
	size_t len = strlen(filename);
	if (len < 4) {
		return false;
	}

	const char *ext = filename + len - 4;
	return (strcasecmp(ext, ".ttf") == 0 || strcasecmp(ext, ".otf") == 0);
}

/**
 * @brief Convert UTF-16BE to UTF-8
 */
static char *convert_utf16be_to_utf8(const uint8_t *utf16be, int length)
{
	/* Allocate worst case: 3 bytes per UTF-16 unit */
	char *utf8 = (char *)malloc(length * 3 / 2 + 1);
	if (utf8 == NULL) {
		return NULL;
	}

	int out_pos = 0;
	for (int i = 0; i < length; i += 2) {
		uint16_t code = (utf16be[i] << 8) | utf16be[i + 1];

		if (code < 0x80) {
			utf8[out_pos++] = (char)code;

		} else if (code < 0x800) {
			utf8[out_pos++] = (char)(0xC0 | (code >> 6));
			utf8[out_pos++] = (char)(0x80 | (code & 0x3F));

		} else {
			utf8[out_pos++] = (char)(0xE0 | (code >> 12));
			utf8[out_pos++] = (char)(0x80 | ((code >> 6) & 0x3F));
			utf8[out_pos++] = (char)(0x80 | (code & 0x3F));
		}
	}
	utf8[out_pos] = '\0';
	return utf8;
}

/**
 * @brief Extract font family name from TTF name table using stb_truetype
 */
static char *parse_font_family_from_ttf(const char *font_path)
{
	FILE *f = fopen(font_path, "rb");
	if (f == NULL) {
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size <= 0 || size > 50 * 1024 * 1024) { /* Sanity check: 50MB max */
		fclose(f);
		return NULL;
	}

	uint8_t *data = (uint8_t *)malloc(size);
	if (data == NULL) {
		fclose(f);
		return NULL;
	}

	size_t read = fread(data, 1, size, f);
	fclose(f);

	if (read != (size_t)size) {
		free(data);
		return NULL;
	}

	/* Parse with stb_truetype */
	stbtt_fontinfo font;
	if (!stbtt_InitFont(&font, data, 0)) {
		free(data);
		return NULL;
	}

	/* Get font family name (nameID=1 for Font Family) */
	int length;
	const char *name = stbtt_GetFontNameString(&font, &length, STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP, STBTT_MS_LANG_ENGLISH, 1); /* nameID 1 = Font Family */

	char *family = NULL;
	if (name && length > 0) {
		/* Convert UTF-16BE to UTF-8 (Microsoft platform) */
		family = convert_utf16be_to_utf8((const uint8_t *)name, length);
	}
	if (family != NULL) {
		/* [F27] the name table is untrusted: no C0 controls / DEL in names shown by --fonts */
		for (char *p = family; *p != '\0'; p++) {
			if ((unsigned char)*p < 0x20 || (unsigned char)*p == 0x7F) {
				*p = '?';
			}
		}
	}

	free(data);
	return family;
}

/**
 * @brief Extract font family name from filename (fallback method)
 */
static char *extract_family_name(const char *filename)
{
	const char *base = strrchr(filename, '/');
	if (base == NULL) {
		base = filename;
	} else {
		base++;
	}

	char *name = strdup(base);
	if (name == NULL) {
		return NULL;
	}

	/* Remove extension */
	char *dot = strrchr(name, '.');
	if (dot != NULL) {
		*dot = '\0';
	}

	/* Remove weight/style suffixes (heuristic) */
	char *dash = strrchr(name, '-');
	if (dash != NULL) {
		*dash = '\0';
	}

	return name;
}

/**
 * @brief Detect font weight from filename
 */
static img2_font_weight_t detect_font_weight(const char *filename)
{
	char buf[256];
	strncpy(buf, filename, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	/* Convert to lowercase for comparison */
	for (char *p = buf; *p; p++) {
		*p = (char)tolower((unsigned char)*p);
	}

	if (strstr(buf, "bold") || strstr(buf, "-b.") || strstr(buf, "-b-")) {
		return IMG2_FONT_WEIGHT_BOLD;
	}

	return IMG2_FONT_WEIGHT_NORMAL;
}

/**
 * @brief Detect font style from filename
 */
static img2_font_style_t detect_font_style(const char *filename)
{
	char buf[256];
	strncpy(buf, filename, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	/* Convert to lowercase */
	for (char *p = buf; *p; p++) {
		*p = tolower(*p);
	}

	if (strstr(buf, "italic") || strstr(buf, "oblique") || strstr(buf, "-i.") || strstr(buf, "-i-")) {
		return IMG2_FONT_STYLE_ITALIC;
	}

	return IMG2_FONT_STYLE_NORMAL;
}

/**
 * @brief Scan a directory for font files
 */
static void scan_font_directory(const char *dir_path)
{
	char *expanded = expand_path(dir_path);
	if (expanded == NULL) {
		return;
	}

	DIR *dir = opendir(expanded);
	if (dir == NULL) {
		free(expanded);
		return;
	}

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_type == DT_DIR) {
			/* Skip . and .. */
			if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
				continue;
			}

			/* Recursively scan subdirectories */
			char subdir[1024];
			snprintf(subdir, sizeof(subdir), "%s/%s", expanded, entry->d_name);
			scan_font_directory(subdir);
			continue;
		}

		if (!is_ttf_file(entry->d_name)) {
			continue;
		}

		/* Build full path */
		char full_path[1024];
		snprintf(full_path, sizeof(full_path), "%s/%s", expanded, entry->d_name);

		/* Extract family name - try TTF parsing first, fallback to filename */
		char *family = parse_font_family_from_ttf(full_path);
		if (family == NULL) {
			family = extract_family_name(entry->d_name);
		}
		if (family == NULL) {
			continue;
		}

		/* Detect weight and style from filename */
		img2_font_weight_t weight = detect_font_weight(entry->d_name);
		img2_font_style_t style = detect_font_style(entry->d_name);

		/* Add to cache */
		font_cache_entry_t *cache_entry = (font_cache_entry_t *)malloc(sizeof(font_cache_entry_t));
		if (cache_entry == NULL) {
			free(family);
			continue;
		}

		cache_entry->family = family;
		cache_entry->path = strdup(full_path);
		cache_entry->weight = weight;
		cache_entry->style = style;
		cache_entry->data = NULL;
		cache_entry->data_size = 0;
		cache_entry->handle = NULL;
		cache_entry->next = g_font_cache;
		g_font_cache = cache_entry;
	}

	closedir(dir);
	free(expanded);
}

/**
 * @brief Initialize font manager (caller holds s_font_lock)
 */
static bool img2_font_manager_init_locked(void)
{
	if (g_initialized) {
		return true;
	}

	/* Scan system font directories */
	for (int i = 0;; i++) {
		const char *path = get_system_font_paths(i);
		if (path == NULL) {
			break;
		}
		scan_font_directory(path);
	}

	/* Add embedded fallback fonts to cache (all 4 variants) */
	/* Regular */
	font_cache_entry_t *regular = (font_cache_entry_t *)malloc(sizeof(font_cache_entry_t));
	if (regular != NULL) {
		regular->family = strdup("DejaVu Sans Mono");
		regular->path = strdup("[embedded]");
		regular->weight = IMG2_FONT_WEIGHT_NORMAL;
		regular->style = IMG2_FONT_STYLE_NORMAL;
		regular->data = (uint8_t *)img2_embedded_dejavu_regular.data;
		regular->data_size = img2_embedded_dejavu_regular.size;
		regular->handle = NULL;
		regular->next = g_font_cache;
		g_font_cache = regular;
	}

	/* Bold */
	font_cache_entry_t *bold = (font_cache_entry_t *)malloc(sizeof(font_cache_entry_t));
	if (bold != NULL) {
		bold->family = strdup("DejaVu Sans Mono");
		bold->path = strdup("[embedded]");
		bold->weight = IMG2_FONT_WEIGHT_BOLD;
		bold->style = IMG2_FONT_STYLE_NORMAL;
		bold->data = (uint8_t *)img2_embedded_dejavu_bold.data;
		bold->data_size = img2_embedded_dejavu_bold.size;
		bold->handle = NULL;
		bold->next = g_font_cache;
		g_font_cache = bold;
	}

	/* Italic */
	font_cache_entry_t *italic = (font_cache_entry_t *)malloc(sizeof(font_cache_entry_t));
	if (italic != NULL) {
		italic->family = strdup("DejaVu Sans Mono");
		italic->path = strdup("[embedded]");
		italic->weight = IMG2_FONT_WEIGHT_NORMAL;
		italic->style = IMG2_FONT_STYLE_ITALIC;
		italic->data = (uint8_t *)img2_embedded_dejavu_italic.data;
		italic->data_size = img2_embedded_dejavu_italic.size;
		italic->handle = NULL;
		italic->next = g_font_cache;
		g_font_cache = italic;
	}

	/* Bold-Italic */
	font_cache_entry_t *bold_italic = (font_cache_entry_t *)malloc(sizeof(font_cache_entry_t));
	if (bold_italic != NULL) {
		bold_italic->family = strdup("DejaVu Sans Mono");
		bold_italic->path = strdup("[embedded]");
		bold_italic->weight = IMG2_FONT_WEIGHT_BOLD;
		bold_italic->style = IMG2_FONT_STYLE_ITALIC;
		bold_italic->data = (uint8_t *)img2_embedded_dejavu_bold_italic.data;
		bold_italic->data_size = img2_embedded_dejavu_bold_italic.size;
		bold_italic->handle = NULL;
		bold_italic->next = g_font_cache;
		g_font_cache = bold_italic;
	}

	/* Keep first embedded font as default fallback */
	g_fallback_font = (img2_font_t *)malloc(sizeof(img2_font_t));
	if (g_fallback_font != NULL) {
		g_fallback_font->family = strdup("DejaVu Sans Mono");
	}
	if (g_fallback_font != NULL && g_fallback_font->family == NULL) {
		/* [F12] never intern a handle with a NULL family: no fallback (img2_font_load_fallback -> NOT_FOUND) */
		free(g_fallback_font);
		g_fallback_font = NULL;
	}
	if (g_fallback_font != NULL) {
		g_fallback_font->weight = IMG2_FONT_WEIGHT_NORMAL;
		g_fallback_font->style = IMG2_FONT_STYLE_NORMAL;
		g_fallback_font->data = img2_embedded_dejavu_regular.data;
		g_fallback_font->data_size = img2_embedded_dejavu_regular.size;
	}

	/* Count fonts in cache */
	int font_count = 0;
	font_cache_entry_t *entry = g_font_cache;
	while (entry != NULL) {
		font_count++;
		entry = entry->next;
	}

	g_initialized = true;
	// fprintf(stderr, "Font manager initialized, found %d fonts (including 4 embedded variants)\n", font_count);

	return true;
}

/**
 * @brief Free a cache-owned handle
 */
static void free_handle(img2_font_t *font)
{
	if (font != NULL) {
		free(font->family);
		free(font);
	}
}

/**
 * @brief Cleanup font manager (caller holds s_font_lock)
 */
static void img2_font_manager_cleanup_locked(void)
{
	if (!g_initialized) {
		return;
	}

	/* Free font cache */
	font_cache_entry_t *entry = g_font_cache;
	while (entry != NULL) {
		font_cache_entry_t *next = entry->next;
		/* Check if data needs to be freed before freeing path */
		bool is_embedded = (entry->path != NULL && strcmp(entry->path, "[embedded]") == 0);
		free_handle(entry->handle);
		free(entry->family);
		free(entry->path);
		/* Only free data if it's not embedded (dynamically loaded from file) */
		if (!is_embedded && entry->data != NULL) {
			free(entry->data);
		}
		free(entry);
		entry = next;
	}
	g_font_cache = NULL;

	/* Free fallback font */
	free_handle(g_fallback_font);
	g_fallback_font = NULL;

	g_initialized = false;
}

void img2_font_manager_cleanup_final(const int *refs)
{
	/* [R19] the caller (img2_shutdown) holds s_init_lock; lock order s_init_lock -> s_font_lock */
	pthread_mutex_lock(&s_font_lock);
	if (refs == NULL || *refs == 0) {
		img2_font_manager_cleanup_locked();
	}
	pthread_mutex_unlock(&s_font_lock);
}

/**
 * @brief Load font file into memory
 */
static bool load_font_file(font_cache_entry_t *entry)
{
	if (entry->data != NULL) {
		return true; /* Already loaded */
	}

	FILE *f = fopen(entry->path, "rb");
	if (f == NULL) {
		return false;
	}

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size <= 0) {
		fclose(f);
		return false;
	}

	entry->data = (uint8_t *)malloc(size);
	if (entry->data == NULL) {
		fclose(f);
		return false;
	}

	size_t read = fread(entry->data, 1, size, f);
	fclose(f);

	if (read != (size_t)size) {
		free(entry->data);
		entry->data = NULL;
		return false;
	}

	entry->data_size = size;
	return true;
}

/**
 * @brief Map generic family names to concrete fonts
 */
static const char *map_generic_family(const char *generic)
{
	if (strcasecmp(generic, "monospace") == 0 || strcasecmp(generic, "mono") == 0 || strcasecmp(generic, "courier") == 0) {
		return "DejaVu Sans Mono";

	} else if (strcasecmp(generic, "sans-serif") == 0 || strcasecmp(generic, "sans") == 0) {
		return "DejaVu Sans";

	} else if (strcasecmp(generic, "serif") == 0) {
		return "DejaVu Serif";
	}
	return generic;
}

/**
 * @brief Find font in cache by exact match
 */
static font_cache_entry_t *find_exact_match(const char *family, img2_font_weight_t weight, img2_font_style_t style)
{
	font_cache_entry_t *entry = g_font_cache;
	while (entry != NULL) {
		if (strcasecmp(entry->family, family) == 0 && entry->weight == weight && entry->style == style) {
			return entry;
		}
		entry = entry->next;
	}
	return NULL;
}

/**
 * @brief Find font in cache by family only (any weight/style)
 */
static font_cache_entry_t *find_family_match(const char *family)
{
	font_cache_entry_t *entry = g_font_cache;
	while (entry != NULL) {
		if (strcasecmp(entry->family, family) == 0) {
			return entry;
		}
		entry = entry->next;
	}
	return NULL;
}

/**
 * @brief Load the embedded fallback font (caller holds s_font_lock)
 */
static img2_font_t *img2_font_find_fallback_locked(void)
{
	if (!g_initialized || g_fallback_font == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Fallback font not available");
		return NULL;
	}

	return g_fallback_font;
}

/**
 * @brief Load a font by family name (caller holds s_font_lock; [R3] calls only *_locked helpers)
 *
 * @param st Set to IMG2_E_NOMEM if building the handle failed, else IMG2_E_NOT_FOUND, when NULL is returned
 */
static img2_font_t *img2_font_find_locked(const char *family, img2_font_weight_t weight, img2_font_style_t style, img2_status_t *st)
{
	*st = IMG2_E_NOT_FOUND;
	if (!g_initialized) {
		img2_log(IMG2_LOG_ERROR, "Error: Font manager not initialized");
		return NULL;
	}

	/* Map generic family names */
	const char *mapped_family = map_generic_family(family);

	/* 1. Try exact match (family + weight + style) */
	font_cache_entry_t *entry = find_exact_match(mapped_family, weight, style);

	/* 2. Try family match with regular weight/style */
	if (entry == NULL && (weight != IMG2_FONT_WEIGHT_NORMAL || style != IMG2_FONT_STYLE_NORMAL)) {
		entry = find_exact_match(mapped_family, IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL);
	}

	/* 3. Try any variant of the family */
	if (entry == NULL) {
		entry = find_family_match(mapped_family);
	}

	/* 4. If still not found and we tried a mapped name, check fallback font name match */
	if (entry == NULL && strcmp(mapped_family, "DejaVu Sans Mono") == 0) {
		/* Use embedded fallback */
		return img2_font_find_fallback_locked();
	}

	/* 5. Try fallback as last resort */
	if (entry == NULL) {
		// fprintf(stderr, "Warning: Font '%s' not found, using fallback\n", family);
		return img2_font_find_fallback_locked();
	}

	/* Load font file if not already loaded */
	if (!load_font_file(entry)) {
		img2_log(IMG2_LOG_WARN, "Warning: Failed to load font '%s', using fallback", family);
		return img2_font_find_fallback_locked();
	}

	/* [R4] Interned handle: built once per cache entry, owned by the cache until the last img2_shutdown() */
	if (entry->handle != NULL) {
		return entry->handle;
	}

	img2_font_t *font = (img2_font_t *)malloc(sizeof(img2_font_t));
	if (font == NULL) {
		*st = IMG2_E_NOMEM;
		return NULL;
	}

	font->family = strdup(entry->family);
	if (font->family == NULL) {
		free(font);
		*st = IMG2_E_NOMEM;
		return NULL;
	}
	font->weight = entry->weight;
	font->style = entry->style;
	font->data = entry->data;
	font->data_size = entry->data_size;

	entry->handle = font;
	return font;
}

/** @brief img2_font_find() with the failure reason in *st (IMG2_E_NOMEM or IMG2_E_NOT_FOUND) */
static img2_font_t *font_find_status(const char *family, img2_font_weight_t weight, img2_font_style_t style, img2_status_t *st)
{
	pthread_mutex_lock(&s_font_lock);
	if (!g_initialized) {
		img2_font_manager_init_locked();
	}
	img2_font_t *font = img2_font_find_locked(family, weight, style, st);
	pthread_mutex_unlock(&s_font_lock);
	return font;
}

img2_font_t *img2_font_find(const char *family, img2_font_weight_t weight, img2_font_style_t style)
{
	img2_status_t st;
	return font_find_status(family, weight, style, &st);
}

img2_font_t *img2_font_find_fallback(void)
{
	pthread_mutex_lock(&s_font_lock);
	if (!g_initialized) {
		img2_font_manager_init_locked();
	}
	img2_font_t *font = img2_font_find_fallback_locked();
	pthread_mutex_unlock(&s_font_lock);
	return font;
}

/**
 * @brief Get TTF font data from a font handle
 */
const uint8_t *img2_font_get_data(const img2_font_t *font, size_t *size_out)
{
	if (font == NULL || size_out == NULL) {
		return NULL;
	}

	*size_out = font->data_size;
	return font->data;
}

/**
 * @brief Get font family name
 */
const char *img2_font_get_family(const img2_font_t *font)
{
	return font ? font->family : NULL;
}

img2_status_t img2_font_load(const char *family, img2_font_weight_t weight, img2_font_style_t style, img2_font_t **out)
{
	if (out == NULL) {
		return IMG2_E_ARG;
	}
	*out = NULL;
	if (family == NULL) {
		return IMG2_E_ARG;
	}
	img2_status_t st = IMG2_E_NOT_FOUND;
	*out = font_find_status(family, weight, style, &st);
	return *out != NULL ? IMG2_OK : st;
}

img2_status_t img2_font_load_fallback(img2_font_t **out)
{
	if (out == NULL) {
		return IMG2_E_ARG;
	}
	*out = img2_font_find_fallback();
	/* the embedded fallback is always built at init; only an allocation failure leaves it NULL */
	return *out != NULL ? IMG2_OK : IMG2_E_NOMEM;
}

img2_status_t img2_font_foreach(img2_font_visit_fn fn, void *user)
{
	if (fn == NULL) {
		return IMG2_E_ARG;
	}

	/* Snapshot the cache under the lock; the visitor runs unlocked (strings live until img2_shutdown()) */
	pthread_mutex_lock(&s_font_lock);
	if (!g_initialized) {
		img2_font_manager_init_locked();
	}
	size_t count = 0;
	for (const font_cache_entry_t *entry = g_font_cache; entry != NULL; entry = entry->next) {
		count++;
	}
	img2_font_info_t *infos = NULL;
	if (count > 0) {
		infos = malloc(count * sizeof(*infos));
		if (infos == NULL) {
			pthread_mutex_unlock(&s_font_lock);
			return IMG2_E_NOMEM;
		}
	}
	size_t i = 0;
	for (const font_cache_entry_t *entry = g_font_cache; entry != NULL; entry = entry->next, i++) {
		infos[i].family = entry->family;
		infos[i].path = entry->path;
		infos[i].weight = entry->weight;
		infos[i].style = entry->style;
		infos[i].embedded = (entry->path != NULL && strcmp(entry->path, "[embedded]") == 0);
	}
	pthread_mutex_unlock(&s_font_lock);

	for (i = 0; i < count; i++) {
		if (fn(&infos[i], user) != 0) {
			break;
		}
	}
	free(infos);
	return IMG2_OK;
}

/* Face over a handle's data (caller holds s_font_lock) */
static img2_font_face_t *img2_font_face_open_locked(const img2_font_t *font)
{
	size_t size = 0;
	const uint8_t *data = img2_font_get_data(font, &size);
	if (data == NULL) {
		return NULL;
	}

	img2_font_face_t *face = malloc(sizeof(*face));
	if (face == NULL) {
		return NULL;
	}
	if (!stbtt_InitFont(&face->info, data, 0)) {
		free(face);
		return NULL;
	}
	return face;
}

img2_font_face_t *img2_font_face_open(const img2_font_t *font)
{
	pthread_mutex_lock(&s_font_lock);
	if (!g_initialized) {
		img2_font_manager_init_locked();
	}
	img2_font_face_t *face = img2_font_face_open_locked(font);
	pthread_mutex_unlock(&s_font_lock);
	return face;
}

void img2_font_face_close(img2_font_face_t *face)
{
	free(face);
}

float img2_font_face_scale_for_pixel_height(const img2_font_face_t *face, float pixels)
{
	return stbtt_ScaleForPixelHeight(&face->info, pixels);
}

void img2_font_face_vmetrics(const img2_font_face_t *face, int *ascent, int *descent, int *line_gap)
{
	stbtt_GetFontVMetrics(&face->info, ascent, descent, line_gap);
}

void img2_font_face_hmetrics(const img2_font_face_t *face, int codepoint, int *advance, int *lsb)
{
	stbtt_GetCodepointHMetrics(&face->info, codepoint, advance, lsb);
}

int img2_font_face_kern(const img2_font_face_t *face, int cp1, int cp2)
{
	return stbtt_GetCodepointKernAdvance(&face->info, cp1, cp2);
}

int img2_font_face_glyph_shape(const img2_font_face_t *face, int codepoint, img2_font_vertex_t **out)
{
	*out = NULL;

	stbtt_vertex *verts = NULL;
	int count = stbtt_GetCodepointShape(&face->info, codepoint, &verts);
	if (count <= 0 || verts == NULL) {
		stbtt_FreeShape(&face->info, verts);
		return 0;
	}

	img2_font_vertex_t *copy = malloc((size_t)count * sizeof(*copy));
	if (copy == NULL) {
		stbtt_FreeShape(&face->info, verts);
		return 0;
	}
	for (int i = 0; i < count; i++) {
		copy[i].x = verts[i].x;
		copy[i].y = verts[i].y;
		copy[i].cx = verts[i].cx;
		copy[i].cy = verts[i].cy;
		copy[i].cx1 = verts[i].cx1;
		copy[i].cy1 = verts[i].cy1;
		copy[i].type = verts[i].type;
	}
	stbtt_FreeShape(&face->info, verts);

	*out = copy;
	return count;
}

void img2_font_face_free_shape(img2_font_vertex_t *v)
{
	free(v);
}
