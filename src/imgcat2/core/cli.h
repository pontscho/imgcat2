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

/**
 * @struct cli_options_t
 * @brief Command-line options structure
 *
 * Stores all parsed command-line options with their default values.
 */
typedef struct {
	char *input_file; /**< Input file path, or NULL for stdin */
	int top_offset; /**< Top offset in terminal rows (default: 8) */
	char *interpolation; /**< Interpolation method: lanczos, bilinear, nearest, cubic */
	bool fit_mode; /**< true = fit to terminal, false = resize to exact dimensions */
	bool silent; /**< true = suppress non-error messages */
	int fps; /**< Animation frames per second (1-15, default: 15) */
	bool animate; /**< true = animate GIF frames */
	int target_width; /**< Target width in pixels (-1 = not specified) */
	int target_height; /**< Target height in pixels (-1 = not specified) */
	bool has_custom_dimensions; /**< true if -w or -h specified */
	bool force_ansi; /**< true = force ANSI rendering (disable iTerm2 protocol) */

	/* internal options */
	struct {
		int rows;
		int cols;
		int width;
		int height;

		bool is_iterm2;
		bool is_ghostty;
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
 * - top_offset is non-negative
 * - fit_mode and resize_mode are not both set
 *
 * @param opts Options structure to validate
 *
 * @return 0 if valid, -1 if invalid (error printed to stderr)
 */
int validate_options(cli_options_t *opts);

#endif /* IMGCAT2_CLI_H */
