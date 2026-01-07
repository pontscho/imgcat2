/**
 * @file cli.c
 * @brief Command-line interface implementation
 *
 * Implements argument parsing with getopt_long(), option validation,
 * and help/version display functions.
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../terminal/terminal.h"
#include "cli.h"

/** Project version from CMake */
#define VERSION_STRING "1.0.0"

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
	printf("  -o, --top-offset N        Top offset in terminal rows (default: 8)\n");
	printf("  -i, --interpolation TYPE  Interpolation method (default: lanczos)\n");
	printf("                            Available: lanczos, bilinear, nearest, cubic\n");
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
	printf("Arguments:\n");
	printf("  FILE                      Input image file (omit or '-' for stdin)\n");
	printf("\n");
	printf("Examples:\n");
	printf("  %s image.png              Display PNG image\n", program_name);
	printf("  %s -a animation.gif       Animate GIF\n", program_name);
	printf("  cat image.jpg | %s        Read from stdin\n", program_name);
	printf("  %s --fps 10 anim.gif      Animate at 10 FPS\n", program_name);
	printf("\n");
}

/**
 * @brief Print version information
 */
void print_version(void)
{
	printf("imgcat2 version %s\n", VERSION_STRING);
	printf("Terminal image viewer with ANSI true color support\n");
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
		{ "help",          no_argument,       0, 'h' },
		{ "version",       no_argument,       0, 'b' },
		{ "top-offset",    required_argument, 0, 'o' },
		{ "interpolation", required_argument, 0, 'i' },
		{ "fit",           no_argument,       0, 'f' },
		{ "resize",        no_argument,       0, 'r' },
		{ "verbose",       no_argument,       0, 'v' },
		{ "fps",           required_argument, 0, 'F' },
		{ "animate",       no_argument,       0, 'a' },
		{ "width",         required_argument, 0, 'w' },
		{ "height",        required_argument, 0, 'H' },
		{ "force-ansi",    no_argument,       0, 'A' },
		{ 0,		       0,		         0, 0   },
	};

	/* Parse options */
	int opt;
	int option_index = 0;

	bool is_iterm2 = terminal_is_iterm2();
	// bool is_ghostty = terminal_is_ghostty();
	if (is_iterm2) {
		opts->fit_mode = false;
	}

	while ((opt = getopt_long(argc, argv, "hbo:i:frvaF:w:H:A", long_options, &option_index)) != -1) {
		switch (opt) {
			case 'h': print_usage(argv[0]); return 1;
			case 'b': print_version(); return 1;
			case 'o': opts->top_offset = atoi(optarg); break;
			case 'i': opts->interpolation = optarg; break;
			case 'f': opts->fit_mode = true; break;
			case 'r': opts->fit_mode = false; break;
			case 'v': opts->silent = false; break;
			case 'F': opts->fps = atoi(optarg); break;
			case 'a': opts->animate = true; break;
			case 'A': opts->force_ansi = true; break;

			case 'w':
				opts->target_width = atoi(optarg);
				opts->has_custom_dimensions = true;
				break;

			case 'H':
				opts->target_height = atoi(optarg);
				opts->has_custom_dimensions = true;
				break;

			case '?':
				/* getopt_long already printed error message */
				fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
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
 * - top_offset is non-negative
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

	/* Validate top_offset is non-negative */
	if (opts->top_offset < 0) {
		fprintf(stderr, "Error: Top offset must be non-negative (got %d)\n", opts->top_offset);
		return -1;
	}

	/* Validate interpolation method */
	if (opts->interpolation != NULL) {
		if (strcmp(opts->interpolation, "lanczos") != 0 && strcmp(opts->interpolation, "bilinear") != 0 && strcmp(opts->interpolation, "nearest") != 0 && strcmp(opts->interpolation, "cubic") != 0) {
			fprintf(stderr, "Error: Invalid interpolation method '%s'\n", opts->interpolation);
			fprintf(stderr, "Valid methods: lanczos, bilinear, nearest, cubic\n");
			return -1;
		}
	}

	/* Validate custom dimensions if specified */
	if (opts->has_custom_dimensions) {
		int rows, cols;

		bool is_iterm2 = terminal_is_iterm2();
		bool is_ghostty = terminal_is_ghostty();

		/* Get terminal dimensions for bounds checking */
		if (terminal_get_size(&rows, &cols) != 0) {
			/* Use defaults if unable to detect */
			rows = 24; /* DEFAULT_TERM_ROWS */
			cols = 80; /* DEFAULT_TERM_COLS */
		}

		int max_width = cols * RESIZE_FACTOR_X;
		int max_height = (rows - opts->top_offset) * RESIZE_FACTOR_Y;

		/* Check width bounds */
		if (opts->target_width > 0) {
			if ((opts->target_width < 1 || opts->target_width > max_width) && ! (is_iterm2 || is_ghostty)) {
				fprintf(stderr, "Error: Width must be between 1 and %d pixels (got %d)\n", max_width, opts->target_width);
				return -1;
			}
		}

		/* Check height bounds */
		if (opts->target_height > 0) {
			if ((opts->target_height < 1 || opts->target_height > max_height) && ! (is_iterm2 || is_ghostty)) {
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

	return 0;
}
