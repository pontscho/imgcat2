/**
 * @file cli.h
 * @brief Command-line interface argument parsing and validation
 *
 * Provides CLI options structure and functions for parsing command-line
 * arguments using getopt_long(), validating options, and displaying
 * help/version information.
 */

#ifndef IMGCAT2_CLI_H
#define IMGCAT2_CLI_H

#include <stdbool.h>

#include <img2/encode.h>
#include <img2/image.h>
#include <img2/yuv.h>

/** Upper bound of -w/--width and -H/--height (pixels) */
#define IMGCAT2_MAX_CUSTOM_DIMENSION 16384

/**
 * @struct cli_options_t
 * @brief Command-line options structure
 *
 * Stores all parsed command-line options with their default values.
 */
typedef struct {
	char *input_file; /**< Input file path, or NULL for stdin */
	char *interpolation; /**< Interpolation method: lanczos, bilinear, nearest, cubic, box, mitchell */
	img2_filter_t filter; /**< Resampling filter mapped from interpolation by validate_options() */
	bool fit_mode; /**< true = fit to terminal, false = resize to exact dimensions */
	bool silent; /**< true = suppress non-error messages */
	int fps; /**< Animation frames per second (1-15, default: 15) */
	bool animate; /**< true = animate GIF frames */
	int target_width; /**< Target width in pixels (-1 = not specified) */
	int target_height; /**< Target height in pixels (-1 = not specified) */
	bool has_custom_dimensions; /**< true if -w or -h specified */
	bool force_ansi; /**< true = force ANSI rendering (disable iTerm2 protocol) */
	bool info_mode; /**< true = output metadata instead of rendering */
	bool json_output; /**< true = format output as JSON */
	bool exif_detailed; /**< true = include all EXIF fields (default: summary only) */
	bool list_fonts; /**< true = list available fonts and exit */

	/* Conversion/encoder options */
	bool convert_mode; /**< true = convert to file instead of terminal rendering */
	img2_format_t output_format; /**< Target output format (IMG2_FORMAT_JPEG, IMG2_FORMAT_PNG) */
	img2_format_t iterm2_format; /**< Format for iTerm2 encoding: IMG2_FORMAT_PNG or IMG2_FORMAT_JPEG */
	char *output_file; /**< Output file path, or NULL for stdout */
	int jpeg_quality; /**< JPEG quality 0-100 (default: 90) */
	int png_compression; /**< PNG compression 0-9 (default: 6) */
	int heif_quality; /**< HEIF quality 0-100 (default: 80) */
	int webp_quality; /**< WebP quality 0-100 (default: 80) */
	int jxl_quality; /**< JXL quality 0-100 (default: 80) */
	int frame_index; /**< Frame to convert for animated images (default: 0) */

	/* Raw input options */
	bool yuv_input; /**< true = input is raw YUV described by yuv (bypasses magic detection) */
	img2_yuv_params_t yuv; /**< Raw YUV geometry (valid when yuv_input), strides resolved */

	/* internal options */
	struct {
		int rows;
		int cols;
		int width;
		int height;

		bool is_iterm2;
		bool is_ghostty;
		bool is_kitty;
		bool is_wezterm;
		bool is_konsole;
		bool is_tmux;
		bool has_kitty;
	} terminal;
} cli_options_t;

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
int parse_arguments(int argc, char **argv, cli_options_t *opts);

/**
 * @brief Print usage help message
 *
 * Prints command-line usage information including all supported
 * options, their descriptions, and usage examples.
 *
 * @param program_name Program name (typically argv[0])
 */
void print_usage(const char *program_name);

/**
 * @brief Print version information
 *
 * Prints program version, build information, and copyright.
 * Version string is defined by CMake project version.
 */
void print_version(void);

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
int validate_options(cli_options_t *opts);

/**
 * @brief Print the font cache (--fonts)
 *
 * Lists every cached font grouped by family, via img2_font_foreach().
 */
void print_font_list(void);

#endif /* IMGCAT2_CLI_H */
