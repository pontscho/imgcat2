/**
 * @file font_manager.c
 * @brief Cross-platform font discovery and management implementation
 */

#define _GNU_SOURCE

#include <sys/stat.h>

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "font_manager.h"

/* Font cache entry */
typedef struct font_cache_entry_t {
	char *family;
	char *path;
	font_weight_t weight;
	font_style_t style;
	uint8_t *data;
	size_t data_size;
	struct font_cache_entry_t *next;
} font_cache_entry_t;

/* Font structure */
struct font_t {
	char *family;
	font_weight_t weight;
	font_style_t style;
	const uint8_t *data;
	size_t data_size;
};

/* Global font cache */
static font_cache_entry_t *g_font_cache = NULL;
static font_t *g_fallback_font = NULL;
static bool g_initialized = false;

/* Embedded fallback fonts */
#include "../font/embedded_fonts.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "../../vendor/stb/stb_truetype.h"

/**
 * @brief Platform-specific font search paths
 */
static const char *get_system_font_paths(int index)
{
#if defined(__linux__)
	static const char *linux_paths[] = { "/usr/share/fonts", "/usr/local/share/fonts", "~/.local/share/fonts", "~/.fonts", NULL };
	return linux_paths[index];

#elif defined(__APPLE__)
	static const char *macos_paths[] = { "/System/Library/Fonts", "/Library/Fonts", "~/Library/Fonts", NULL };
	return macos_paths[index];

#elif defined(_WIN32)
	static const char *windows_paths[] = { "C:\\Windows\\Fonts", NULL };
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

	const char *home = getenv("HOME");
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
static font_weight_t detect_font_weight(const char *filename)
{
	char buf[256];
	strncpy(buf, filename, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	/* Convert to lowercase for comparison */
	for (char *p = buf; *p; p++) {
		*p = (char)tolower((unsigned char)*p);
	}

	if (strstr(buf, "bold") || strstr(buf, "-b.") || strstr(buf, "-b-")) {
		return FONT_WEIGHT_BOLD;
	}

	return FONT_WEIGHT_NORMAL;
}

/**
 * @brief Detect font style from filename
 */
static font_style_t detect_font_style(const char *filename)
{
	char buf[256];
	strncpy(buf, filename, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	/* Convert to lowercase */
	for (char *p = buf; *p; p++) {
		*p = tolower(*p);
	}

	if (strstr(buf, "italic") || strstr(buf, "oblique") || strstr(buf, "-i.") || strstr(buf, "-i-")) {
		return FONT_STYLE_ITALIC;
	}

	return FONT_STYLE_NORMAL;
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
		font_weight_t weight = detect_font_weight(entry->d_name);
		font_style_t style = detect_font_style(entry->d_name);

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
		cache_entry->next = g_font_cache;
		g_font_cache = cache_entry;
	}

	closedir(dir);
	free(expanded);
}

/**
 * @brief Initialize font manager
 */
bool font_manager_init(void)
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
		regular->weight = FONT_WEIGHT_NORMAL;
		regular->style = FONT_STYLE_NORMAL;
		regular->data = (uint8_t *)embedded_dejavu_regular.data;
		regular->data_size = embedded_dejavu_regular.size;
		regular->next = g_font_cache;
		g_font_cache = regular;
	}

	/* Bold */
	font_cache_entry_t *bold = (font_cache_entry_t *)malloc(sizeof(font_cache_entry_t));
	if (bold != NULL) {
		bold->family = strdup("DejaVu Sans Mono");
		bold->path = strdup("[embedded]");
		bold->weight = FONT_WEIGHT_BOLD;
		bold->style = FONT_STYLE_NORMAL;
		bold->data = (uint8_t *)embedded_dejavu_bold.data;
		bold->data_size = embedded_dejavu_bold.size;
		bold->next = g_font_cache;
		g_font_cache = bold;
	}

	/* Italic */
	font_cache_entry_t *italic = (font_cache_entry_t *)malloc(sizeof(font_cache_entry_t));
	if (italic != NULL) {
		italic->family = strdup("DejaVu Sans Mono");
		italic->path = strdup("[embedded]");
		italic->weight = FONT_WEIGHT_NORMAL;
		italic->style = FONT_STYLE_ITALIC;
		italic->data = (uint8_t *)embedded_dejavu_italic.data;
		italic->data_size = embedded_dejavu_italic.size;
		italic->next = g_font_cache;
		g_font_cache = italic;
	}

	/* Bold-Italic */
	font_cache_entry_t *bold_italic = (font_cache_entry_t *)malloc(sizeof(font_cache_entry_t));
	if (bold_italic != NULL) {
		bold_italic->family = strdup("DejaVu Sans Mono");
		bold_italic->path = strdup("[embedded]");
		bold_italic->weight = FONT_WEIGHT_BOLD;
		bold_italic->style = FONT_STYLE_ITALIC;
		bold_italic->data = (uint8_t *)embedded_dejavu_bold_italic.data;
		bold_italic->data_size = embedded_dejavu_bold_italic.size;
		bold_italic->next = g_font_cache;
		g_font_cache = bold_italic;
	}

	/* Keep first embedded font as default fallback */
	g_fallback_font = (font_t *)malloc(sizeof(font_t));
	if (g_fallback_font != NULL) {
		g_fallback_font->family = strdup("DejaVu Sans Mono");
		g_fallback_font->weight = FONT_WEIGHT_NORMAL;
		g_fallback_font->style = FONT_STYLE_NORMAL;
		g_fallback_font->data = embedded_dejavu_regular.data;
		g_fallback_font->data_size = embedded_dejavu_regular.size;
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
 * @brief Cleanup font manager
 */
void font_manager_cleanup(void)
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
	if (g_fallback_font != NULL) {
		free(g_fallback_font->family);
		free(g_fallback_font);
		g_fallback_font = NULL;
	}

	g_initialized = false;
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
static font_cache_entry_t *find_exact_match(const char *family, font_weight_t weight, font_style_t style)
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
 * @brief Load a font by family name
 */
font_t *font_manager_load(const char *family, font_weight_t weight, font_style_t style)
{
	if (!g_initialized) {
		fprintf(stderr, "Error: Font manager not initialized\n");
		return NULL;
	}

	/* Map generic family names */
	const char *mapped_family = map_generic_family(family);

	/* 1. Try exact match (family + weight + style) */
	font_cache_entry_t *entry = find_exact_match(mapped_family, weight, style);

	/* 2. Try family match with regular weight/style */
	if (entry == NULL && (weight != FONT_WEIGHT_NORMAL || style != FONT_STYLE_NORMAL)) {
		entry = find_exact_match(mapped_family, FONT_WEIGHT_NORMAL, FONT_STYLE_NORMAL);
	}

	/* 3. Try any variant of the family */
	if (entry == NULL) {
		entry = find_family_match(mapped_family);
	}

	/* 4. If still not found and we tried a mapped name, check fallback font name match */
	if (entry == NULL && strcmp(mapped_family, "DejaVu Sans Mono") == 0) {
		/* Use embedded fallback */
		return font_manager_load_fallback();
	}

	/* 5. Try fallback as last resort */
	if (entry == NULL) {
		fprintf(stderr, "Warning: Font '%s' not found, using fallback\n", family);
		return font_manager_load_fallback();
	}

	/* Load font file if not already loaded */
	if (!load_font_file(entry)) {
		fprintf(stderr, "Warning: Failed to load font '%s', using fallback\n", family);
		return font_manager_load_fallback();
	}

	/* Create font handle */
	font_t *font = (font_t *)malloc(sizeof(font_t));
	if (font == NULL) {
		return NULL;
	}

	font->family = strdup(entry->family);
	font->weight = entry->weight;
	font->style = entry->style;
	font->data = entry->data;
	font->data_size = entry->data_size;

	return font;
}

/**
 * @brief Load the embedded fallback font
 */
font_t *font_manager_load_fallback(void)
{
	if (!g_initialized || g_fallback_font == NULL) {
		fprintf(stderr, "Error: Fallback font not available\n");
		return NULL;
	}

	return g_fallback_font;
}

/**
 * @brief Get TTF font data from a font handle
 */
const uint8_t *font_get_data(const font_t *font, size_t *size_out)
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
const char *font_get_family(const font_t *font)
{
	return font ? font->family : NULL;
}

/**
 * @brief Helper to get weight name
 */
static const char *weight_to_string(font_weight_t weight)
{
	switch (weight) {
		case FONT_WEIGHT_NORMAL: return "regular";
		case FONT_WEIGHT_BOLD: return "bold";
		default: return "unknown";
	}
}

/**
 * @brief Helper to get style name
 */
static const char *style_to_string(font_style_t style)
{
	switch (style) {
		case FONT_STYLE_NORMAL: return "normal";
		case FONT_STYLE_ITALIC: return "italic";
		default: return "unknown";
	}
}

/**
 * @brief List all available fonts
 */
void font_manager_list_fonts(void)
{
	if (!g_initialized) {
		fprintf(stderr, "Error: Font manager not initialized\n");
		return;
	}

	/* Count unique font families */
	int total_count = 0;
	int embedded_count = 0;

	/* Create a sorted list of unique family names */
	typedef struct family_list_t {
		char *family;
		struct family_list_t *next;
	} family_list_t;

	family_list_t *families = NULL;

	/* First pass: collect unique families */
	font_cache_entry_t *entry = g_font_cache;
	while (entry != NULL) {
		/* Check if family already in list */
		bool found = false;
		family_list_t *fam = families;
		while (fam != NULL) {
			if (strcmp(fam->family, entry->family) == 0) {
				found = true;
				break;
			}
			fam = fam->next;
		}

		if (!found) {
			/* Add new family */
			family_list_t *new_fam = (family_list_t *)malloc(sizeof(family_list_t));
			if (new_fam != NULL) {
				new_fam->family = entry->family;
				new_fam->next = families;
				families = new_fam;
			}
		}

		total_count++;
		if (strcmp(entry->path, "[embedded]") == 0) {
			embedded_count++;
		}
		entry = entry->next;
	}

	printf("Available fonts (%d total, %d embedded):\n\n", total_count, embedded_count);

	/* Second pass: print fonts grouped by family */
	family_list_t *fam = families;
	while (fam != NULL) {
		printf("%s: ", fam->family);

		/* Find all variants of this family */
		bool first = true;
		entry = g_font_cache;
		while (entry != NULL) {
			if (strcmp(entry->family, fam->family) == 0) {
				if (!first) {
					printf(", ");
				}
				printf("%s/%s", weight_to_string(entry->weight), style_to_string(entry->style));
				first = false;
			}
			entry = entry->next;
		}
		printf("\n");

		fam = fam->next;
	}

	/* Free family list */
	while (families != NULL) {
		family_list_t *next = families->next;
		free(families);
		families = next;
	}
}
