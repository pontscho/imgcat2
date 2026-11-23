/**
 * @file cli.c
 * @brief Command-line interface implementation
 *
 * Implements argument parsing with getopt_long(), option validation,
 * and help/version display functions.
 */

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <img2/img2.h>

#include "terminal/terminal.h"
#include "cli.h"
#include "safe_text.h"

/** Project version from CMake (IMGCAT2_VERSION_STRING compile definition) */
#ifndef IMGCAT2_VERSION_STRING
#define IMGCAT2_VERSION_STRING "1.0.0"
#endif

/** Resize factors (must match pipeline.c) */
#define RESIZE_FACTOR_X 1
#define RESIZE_FACTOR_Y 2

/**
 * @brief Print usage help message
 */
void print_usage(const char *program_name)
{
	printf("Usage: %s [OPTIONS] [FILE]\n", program_name);
	printf("\n");
	printf("Display images in the terminal using ANSI escape sequences and half-block characters.\n");
	printf("\n");
	printf("Options:\n");
	printf("  -h, --help                Show this help message and exit\n");
	printf("      --version             Show version information and exit\n");
	printf("      --fonts               List available fonts and exit\n");
	printf("  -i, --interpolation TYPE  Interpolation method (default: lanczos)\n");
	printf("                            Available: lanczos, bilinear, nearest, cubic, box, mitchell\n");
	printf("                            (lanczos = stb_image_resize2 default; stb has no Lanczos filter)\n");
	printf("  -f, --fit                 Fit image to terminal (maintain aspect ratio, default)\n");
	printf("  -r, --resize              Resize to exact terminal dimensions (may distort)\n");
	printf("  -w, --width N             Target width in pixels\n");
	printf("  -H, --height N            Target height in pixels\n");
	printf("                            If both: exact dimensions\n");
	printf("                            If one: aspect ratio preserved\n");
	printf("                            If neither: fit to terminal (default)\n");
	printf("  -v, --verbose             Verbose mode (show non-error messages)\n");
	printf("      --fps N               Animation FPS (1-15, default: 15)\n");
	printf("  -a, --animate             Animate GIF frames\n");
	printf("      --force-ansi          Force ANSI rendering (disable iTerm2 protocol)\n");
	printf("\n");
	printf("Metadata Options:\n");
	printf("      --info                Output image metadata instead of rendering\n");
	printf("      --json                Format --info output as JSON (single line)\n");
	printf("      --exif-detailed       Include all EXIF fields (default: summary only)\n");
	printf("\n");
	printf("Conversion Options:\n");
	printf("  -j, --jpeg <quality>      Convert to JPEG format (0-100, default: 90)\n");
	printf("  -p, --png <level>         Convert to PNG format (0-9, default: 6)\n");
	printf("      --heif <quality>      Convert to HEIF (quality: 0-100, default: 80)\n");
	printf("      --webp <quality>      Convert to WebP (quality: 0-100, default: 80, 100=lossless)\n");
	printf("      --jxl <quality>       Convert to JXL (quality: 0-100, default: 80, >=95=lossless)\n");
	printf("  -o, --output <file>       Output file (stdout if not specified)\n");
	printf("  -n, --frame <index>       Select frame for animated images (default: 0)\n");
	printf("      --iterm2-format <png|jpeg>  Format for iTerm2 encoding (default: jpeg)\n");
	printf("                            png: lossless, transparency support\n");
	printf("                            jpeg: smaller files, no transparency\n");
	printf("\n");
	printf("Input Options:\n");
	printf("      --yuv FMT:W:H[:YSTRIDE][:UVSTRIDE]\n");
	printf("                            Treat input as a raw 8-bit YUV frame (no format detection)\n");
	printf("                            FMT: i420 (yuv420p), yv12, nv12, nv21, i422 (yuv422p),\n");
	printf("                                 i444 (yuv444p), yuyv (yuy2), uyvy, gray (y8, y800)\n");
	printf("                            YSTRIDE: bytes per luma/packed row (default: minimal)\n");
	printf("                            UVSTRIDE: bytes per chroma row (default: minimal)\n");
	printf("                            Planes are contiguous (Y, U, V; yv12: Y, V, U);\n");
	printf("                            BT.601 limited range; only the first frame is used\n");
	printf("\n");
	printf("Arguments:\n");
	printf("  FILE                      Input image file (omit or '-' for stdin)\n");
	printf("\n");
	printf("Examples:\n");
	printf("  %s image.png              Display PNG image\n", program_name);
	printf("  %s -a animation.gif       Animate GIF\n", program_name);
	printf("  cat image.jpg | %s        Read from stdin\n", program_name);
	printf("  %s --fps 10 anim.gif      Animate at 10 FPS\n", program_name);
	printf("  %s --jpeg 90 img.png > out.jpg  Convert to JPEG (stdout)\n", program_name);
	printf("  %s --png 6 --output out.png img.jpg  Convert to PNG file\n", program_name);
	printf("  %s --yuv nv12:1920:1080 frame.yuv  Display a raw NV12 frame\n", program_name);
	printf("\n");
}

/**
 * @brief Print version information
 */
void print_version(void)
{
	printf("imgcat2 version %s\n", IMGCAT2_VERSION_STRING);
	printf("Modern terminal image viewer with true color support\n");
	printf("\n");
	printf("Build information:\n");
	printf("  Platform: ");
#if defined(__APPLE__)
	printf("macOS\n");
#elif defined(__linux__)
	printf("Linux\n");
#elif defined(_WIN32)
	printf("Windows\n");
#else
	printf("Unknown\n");
#endif
	printf("  Compiler: ");
#if defined(__clang__)
	printf("Clang %d.%d.%d\n", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
	printf("GCC %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
	printf("MSVC %d\n", _MSC_VER);
#else
	printf("Unknown\n");
#endif
	printf("\n");
	printf("Supported image formats:\n");

	/* Print each supported format (const table, no init needed) */
	for (size_t i = 0; i < img2_decoder_count(); i++) {
		printf("  - %s\n", img2_decoder_info(i)->name);
	}
	printf("  - YUV raw (via --yuv)\n");
}

/** Snapshot of the font cache collected by print_font_list() */
typedef struct {
	img2_font_info_t *items;
	size_t count;
	size_t cap;
} font_list_t;

static int collect_font(const img2_font_info_t *info, void *user)
{
	font_list_t *list = user;
	if (list->count == list->cap) {
		size_t cap = list->cap ? list->cap * 2 : 64;
		img2_font_info_t *items = realloc(list->items, cap * sizeof(*items));
		if (items == NULL) {
			return 1;
		}
		list->items = items;
		list->cap = cap;
	}
	list->items[list->count++] = *info;
	return 0;
}

static const char *font_weight_to_string(img2_font_weight_t weight)
{
	switch (weight) {
		case IMG2_FONT_WEIGHT_NORMAL: return "regular";
		case IMG2_FONT_WEIGHT_BOLD: return "bold";
		default: return "unknown";
	}
}

static const char *font_style_to_string(img2_font_style_t style)
{
	switch (style) {
		case IMG2_FONT_STYLE_NORMAL: return "normal";
		case IMG2_FONT_STYLE_ITALIC: return "italic";
		default: return "unknown";
	}
}

/**
 * @brief Print the font cache grouped by family (P11 format)
 */
void print_font_list(void)
{
	font_list_t list = { NULL, 0, 0 };
	img2_font_foreach(collect_font, &list);

	/* Count fonts; build the family list by prepending first occurrences */
	int total_count = 0;
	int embedded_count = 0;

	typedef struct family_list_t {
		const char *family;
		struct family_list_t *next;
	} family_list_t;

	family_list_t *families = NULL;

	for (size_t i = 0; i < list.count; i++) {
		const img2_font_info_t *entry = &list.items[i];
		bool found = false;
		for (family_list_t *fam = families; fam != NULL; fam = fam->next) {
			if (strcmp(fam->family, entry->family) == 0) {
				found = true;
				break;
			}
		}

		if (!found) {
			family_list_t *new_fam = (family_list_t *)malloc(sizeof(family_list_t));
			if (new_fam != NULL) {
				new_fam->family = entry->family;
				new_fam->next = families;
				families = new_fam;
			}
		}

		total_count++;
		if (entry->embedded) {
			embedded_count++;
		}
	}

	printf("Available fonts (%d total, %d embedded):\n\n", total_count, embedded_count);

	/* Print fonts grouped by family */
	for (family_list_t *fam = families; fam != NULL; fam = fam->next) {
		cli_fputs_safe(fam->family, stdout); /* [F27] family names come from font files */
		fputs(": ", stdout);

		bool first = true;
		for (size_t i = 0; i < list.count; i++) {
			const img2_font_info_t *entry = &list.items[i];
			if (strcmp(entry->family, fam->family) == 0) {
				if (!first) {
					printf(", ");
				}
				printf("%s/%s", font_weight_to_string(entry->weight), font_style_to_string(entry->style));
				first = false;
			}
		}
		printf("\n");
	}

	while (families != NULL) {
		family_list_t *next = families->next;
		free(families);
		families = next;
	}
	free(list.items);
}

/**
 * @brief Parse a -w/-H value: decimal integer in [0, IMGCAT2_MAX_CUSTOM_DIMENSION], nothing else
 *
 * 0 means "not given / auto" (e.g. `-w 0 -H 200` scales by height only).
 *
 * @return 0 on success, -1 on error (error printed to stderr)
 */
static int parse_dimension_arg(const char *arg, const char *what, int *out)
{
	char *end = NULL;
	errno = 0;
	long v = strtol(arg, &end, 10);
	if (errno != 0 || end == arg || *end != '\0' || v < 0 || v > IMGCAT2_MAX_CUSTOM_DIMENSION) {
		char safe[CLI_SAFE_PATH_SIZE]; /* [P39] the argument with control characters escaped */
		fprintf(stderr, "Error: %s must be between 0 (auto) and %d pixels (got '%s')\n", what, IMGCAT2_MAX_CUSTOM_DIMENSION, cli_safe_str(arg, safe, sizeof(safe)));
		return -1;
	}
	*out = (int)v;
	return 0;
}

/**
 * @brief Parse command-line arguments
 *
 * Parses command-line arguments using getopt_long() and populates
 * the cli_options_t structure with parsed values. Handles --help
 * and --version flags by printing and exiting.
 *
 * @param argc Argument count from main()
 * @param argv Argument vector from main()
 * @param opts Output parameter for parsed options
 *
 * @return 0 on success, -1 on error, 1 if help/version was
 *
 * @note Caller must initialize opts with default values before calling
 * @note Sets opts->input_file to NULL for stdin or argv[optind] for file
 */
int parse_arguments(int argc, char **argv, cli_options_t *opts)
{
	if (opts == NULL) {
		return -1;
	}

	/* Long options definition */
	static struct option long_options[] = {
		{ "help",          no_argument,       0, 'h'  },
		{ "version",       no_argument,       0, 'b'  },
		{ "interpolation", required_argument, 0, 'i'  },
		{ "fit",           no_argument,       0, 'f'  },
		{ "resize",        no_argument,       0, 'r'  },
		{ "verbose",       no_argument,       0, 'v'  },
		{ "fps",           required_argument, 0, 'F'  },
		{ "animate",       no_argument,       0, 'a'  },
		{ "width",         required_argument, 0, 'w'  },
		{ "height",        required_argument, 0, 'H'  },
		{ "force-ansi",    no_argument,       0, 'A'  },
		{ "info",          no_argument,       0, 'I'  },
		{ "json",          no_argument,       0, 'J'  },
		{ "exif-detailed", no_argument,       0, 'E'  },
		{ "fonts",         no_argument,       0, 'L'  },
		{ "jpeg",          required_argument, 0, 'j'  },
		{ "png",           required_argument, 0, 'p'  },
		{ "heif",          required_argument, 0, 1000 },
		{ "webp",          required_argument, 0, 1001 },
		{ "jxl",           required_argument, 0, 1002 },
		{ "output",        required_argument, 0, 'o'  },
		{ "frame",         required_argument, 0, 'n'  },
		{ "iterm2-format", required_argument, 0, 't'  },
		{ "yuv",           required_argument, 0, 1003 },
		{ 0,		       0,		         0, 0    },
	};

	/* Parse options */
	int opt;
	int option_index = 0;
	char safe[CLI_SAFE_PATH_SIZE];  /* [P39] an argument echoed in an error message, control characters escaped */
	char safe2[CLI_SAFE_PATH_SIZE]; /* a second one in the same message */

	/* Short forms take an argument exactly when their long option does (-b = --version: none; -F = --fps: one) */
	while ((opt = getopt_long(argc, argv, "hbi:frvaF:t:w:H:AIJELj:p:o:n:", long_options, &option_index)) != -1) {
		switch (opt) {
			case 'h': print_usage(argv[0]); return 1;
			case 'b': print_version(); return 1;
			case 'i': opts->interpolation = optarg; break;
			case 'f': opts->fit_mode = true; break;
			case 'r': opts->fit_mode = false; break;
			case 'v': opts->silent = false; break;
			case 'F': opts->fps = atoi(optarg); break;
			case 'a': opts->animate = true; break;
			case 'A': opts->force_ansi = true; break;
			case 'I': opts->info_mode = true; break;
			case 'J': opts->json_output = true; break;
			case 'E': opts->exif_detailed = true; break;
			case 'L': opts->list_fonts = true; break;

			case 'w':
				if (parse_dimension_arg(optarg, "Width", &opts->target_width) < 0) {
					return -1;
				}
				opts->has_custom_dimensions = true;
				break;

			case 'H':
				if (parse_dimension_arg(optarg, "Height", &opts->target_height) < 0) {
					return -1;
				}
				opts->has_custom_dimensions = true;
				break;

			case 'j':
				opts->convert_mode = true;
				opts->output_format = IMG2_FORMAT_JPEG;
				opts->jpeg_quality = atoi(optarg);
				break;

			case 'p':
				opts->convert_mode = true;
				opts->output_format = IMG2_FORMAT_PNG;
				opts->png_compression = atoi(optarg);
				break;

			case 1000: /* --heif */
				opts->convert_mode = true;
				opts->output_format = IMG2_FORMAT_HEIF;
				opts->heif_quality = atoi(optarg);
				break;

			case 1001: /* --webp */
				opts->convert_mode = true;
				opts->output_format = IMG2_FORMAT_WEBP;
				opts->webp_quality = atoi(optarg);
				break;

			case 1002: /* --jxl */
				opts->convert_mode = true;
				opts->output_format = IMG2_FORMAT_JXL;
				opts->jxl_quality = atoi(optarg);
				break;

			case 1003: /* --yuv */
			{
				char err[160];
				if (img2_yuv_parse_spec(optarg, &opts->yuv, err, sizeof(err)) < 0) {
					fprintf(stderr, "Error: Invalid --yuv '%s': %s\n", cli_safe_str(optarg, safe, sizeof(safe)), cli_safe_str(err, safe2, sizeof(safe2)));
					fprintf(stderr, "Usage: --yuv format:width:height[:y_stride][:uv_stride]\n");
					return -1;
				}
				opts->yuv_input = true;
				break;
			}

			case 'o': opts->output_file = optarg; break;

			case 'n': opts->frame_index = atoi(optarg); break;

			case 't':
				/* Long-only option */
				if (strcmp(long_options[option_index].name, "iterm2-format") == 0) {
					/* Parse iterm2-format argument */
					if (strcasecmp(optarg, "png") == 0) {
						opts->iterm2_format = IMG2_FORMAT_PNG;
					} else if (strcasecmp(optarg, "jpeg") == 0) {
						opts->iterm2_format = IMG2_FORMAT_JPEG;
					} else {
						fprintf(stderr, "Error: Invalid iterm2-format '%s' (must be 'png' or 'jpeg')\n", cli_safe_str(optarg, safe, sizeof(safe)));
						return -1;
					}
				}
				break;

			case '?':
				/* getopt_long already printed error message */
				fprintf(stderr, "Try '%s --help' for more information.\n", cli_safe_str(argv[0], safe, sizeof(safe)));
				return -1;

			default: return -1;
		}
	}

	/* Parse positional argument (input file) */
	if (optind < argc) {
		/* Check if input is "-" (stdin) */
		if (strcmp(argv[optind], "-") == 0) {
			opts->input_file = NULL;

		} else {
			opts->input_file = argv[optind];
		}

	} else {
		/* No file specified, use stdin */
		opts->input_file = NULL;
	}

	return 0;
}

/**
 * @brief Validate CLI options
 *
 * Validates parsed command-line options for correctness:
 * - fps in range [1, 15]
 * - interpolation is valid method
 * - fit_mode and resize_mode are not both set
 *
 * @param opts Options structure to validate
 *
 * @return 0 if valid, -1 if invalid (error printed to stderr)
 */
int validate_options(cli_options_t *opts)
{
	if (opts == NULL) {
		return -1;
	}

	/* Validate FPS range */
	if (opts->fps < 1 || opts->fps > 15) {
		fprintf(stderr, "Error: FPS must be between 1 and 15 (got %d)\n", opts->fps);
		return -1;
	}

	/* Validate interpolation method */
	opts->filter = IMG2_FILTER_DEFAULT;
	if (opts->interpolation != NULL) {
		static const struct {
			const char *name;
			img2_filter_t filter;
		} methods[] = {
			{ "lanczos",  IMG2_FILTER_DEFAULT  }, /* stb has no Lanczos: the stb_image_resize2 sRGB default */
			{ "bilinear", IMG2_FILTER_TRIANGLE },
			{ "nearest",  IMG2_FILTER_POINT    },
			{ "cubic",    IMG2_FILTER_CUBIC    },
			{ "box",      IMG2_FILTER_BOX      },
			{ "mitchell", IMG2_FILTER_MITCHELL },
		};
		size_t i;
		for (i = 0; i < sizeof(methods) / sizeof(methods[0]); i++) {
			if (strcmp(opts->interpolation, methods[i].name) == 0) {
				break;
			}
		}
		if (i == sizeof(methods) / sizeof(methods[0])) {
			char safe[CLI_SAFE_PATH_SIZE]; /* [P39] the argument with control characters escaped */
			fprintf(stderr, "Error: Invalid interpolation method '%s'\n", cli_safe_str(opts->interpolation, safe, sizeof(safe)));
			fprintf(stderr, "Valid methods: lanczos, bilinear, nearest, cubic, box, mitchell\n");
			return -1;
		}
		opts->filter = methods[i].filter;
	}

	/* Validate custom dimensions if specified */
	if (opts->has_custom_dimensions) {
		int max_width = opts->terminal.cols * RESIZE_FACTOR_X;
		int max_height = opts->terminal.rows * RESIZE_FACTOR_Y;

		/* Check width bounds */
		if (opts->target_width > 0) {
			if ((opts->target_width < 1 || opts->target_width > max_width) && !(opts->terminal.is_iterm2 || opts->terminal.has_kitty)) {
				fprintf(stderr, "Error: Width must be between 1 and %d pixels (got %d)\n", max_width, opts->target_width);
				return -1;
			}
		}

		/* Check height bounds */
		if (opts->target_height > 0) {
			if ((opts->target_height < 1 || opts->target_height > max_height) && !(opts->terminal.is_iterm2 || opts->terminal.has_kitty)) {
				fprintf(stderr, "Error: Height must be between 1 and %d pixels (got %d)\n", max_height, opts->target_height);
				return -1;
			}
		}

		/* At least one dimension must be positive */
		if (opts->target_width <= 0 && opts->target_height <= 0) {
			fprintf(stderr, "Error: At least one of -w or -h must be positive\n");
			return -1;
		}
	}

	/* Validate that --json is only used with --info */
	if (opts->json_output && !opts->info_mode) {
		fprintf(stderr, "Error: --json can only be used with --info\n");
		return -1;
	}

	/* Validate encoder options */
	if (opts->convert_mode) {
		/* Require output format to be set */
		if (opts->output_format == IMG2_FORMAT_NONE) {
			fprintf(stderr, "Error: Conversion mode requires --jpeg or --png\n");
			return -1;
		}

		/* Validate frame index */
		if (opts->frame_index < 0) {
			fprintf(stderr, "Error: Frame index must be non-negative (got %d)\n", opts->frame_index);
			return -1;
		}
	}

	/* Clamp JPEG quality to valid range [0, 100] */
	if (opts->jpeg_quality < 0) {
		fprintf(stderr, "Warning: JPEG quality %d < 0, clamping to 0\n", opts->jpeg_quality);
		opts->jpeg_quality = 0;
	}
	if (opts->jpeg_quality > 100) {
		fprintf(stderr, "Warning: JPEG quality %d > 100, clamping to 100\n", opts->jpeg_quality);
		opts->jpeg_quality = 100;
	}

	/* Clamp PNG compression to valid range [0, 9] */
	if (opts->png_compression < 0) {
		fprintf(stderr, "Warning: PNG compression %d < 0, clamping to 0\n", opts->png_compression);
		opts->png_compression = 0;
	}
	if (opts->png_compression > 9) {
		fprintf(stderr, "Warning: PNG compression %d > 9, clamping to 9\n", opts->png_compression);
		opts->png_compression = 9;
	}

	/* Validate HEIF quality range [0, 100] */
	if (opts->heif_quality < 0 || opts->heif_quality > 100) {
		fprintf(stderr, "Error: HEIF quality must be 0-100 (got %d)\n", opts->heif_quality);
		return -1;
	}

	/* Validate WebP quality range [0, 100] */
	if (opts->webp_quality < 0 || opts->webp_quality > 100) {
		fprintf(stderr, "Error: WebP quality must be 0-100 (got %d)\n", opts->webp_quality);
		return -1;
	}

	/* Validate JXL quality range [0, 100] */
	if (opts->jxl_quality < 0 || opts->jxl_quality > 100) {
		fprintf(stderr, "Error: JXL quality must be 0-100 (got %d)\n", opts->jxl_quality);
		return -1;
	}

	return 0;
}
