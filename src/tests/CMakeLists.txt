# Test suite for imgcat2

# Unit tests
add_executable(test_terminal
	unit/main.c
	unit/test_terminal.c
)

target_link_libraries(test_terminal
	imgcat2_lib
)

# Add test to CTest
add_test(NAME test_terminal COMMAND test_terminal)

# Set test properties
set_tests_properties(test_terminal PROPERTIES
	TIMEOUT 10
)

# ANSI tests (task-065)
add_executable(test_ansi
	unit/main.c
	unit/test_ansi.c
)

target_link_libraries(test_ansi
	imgcat2_lib
)

add_test(NAME test_ansi COMMAND test_ansi)

set_tests_properties(test_ansi PROPERTIES
	TIMEOUT 10
)

# Security tests (task-067)
add_executable(test_security
	unit/main.c
	unit/test_security.c
)

target_link_libraries(test_security
	imgcat2_lib
)

add_test(NAME test_security COMMAND test_security)

set_tests_properties(test_security PROPERTIES
	TIMEOUT 10
)

# Magic bytes tests (task-068)
add_executable(test_magic
	unit/main.c
	unit/test_magic.c
)

target_link_libraries(test_magic
	imgcat2_lib
)

add_test(NAME test_magic COMMAND test_magic)

set_tests_properties(test_magic PROPERTIES
	TIMEOUT 10
)

# CLI parsing tests (task-070)
add_executable(test_cli_parsing
	unit/main.c
	unit/test_cli_parsing.c
)

target_link_libraries(test_cli_parsing
	imgcat2_lib
)

add_test(NAME test_cli_parsing COMMAND test_cli_parsing)

set_tests_properties(test_cli_parsing PROPERTIES
	TIMEOUT 10
)

# PNG decoder tests (task-061)
add_executable(test_decoder_png
	unit/main.c
	unit/test_decoder_png.c
)

target_link_libraries(test_decoder_png
	imgcat2_lib
)

add_test(NAME test_decoder_png COMMAND test_decoder_png)

set_tests_properties(test_decoder_png PROPERTIES
	TIMEOUT 10
)

# JPEG decoder tests (task-062)
add_executable(test_decoder_jpeg
	unit/main.c
	unit/test_decoder_jpeg.c
)

target_link_libraries(test_decoder_jpeg
	imgcat2_lib
)

add_test(NAME test_decoder_jpeg COMMAND test_decoder_jpeg)

set_tests_properties(test_decoder_jpeg PROPERTIES
	TIMEOUT 10
)

# STB decoder tests (task-063)
add_executable(test_decoder_stb
	unit/main.c
	unit/test_decoder_stb.c
)

target_link_libraries(test_decoder_stb
	imgcat2_lib
)

add_test(NAME test_decoder_stb COMMAND test_decoder_stb)

set_tests_properties(test_decoder_stb PROPERTIES
	TIMEOUT 10
)

# Image processing tests (task-066)
add_executable(test_image_processing
	unit/main.c
	unit/test_image_processing.c
)

target_link_libraries(test_image_processing
	imgcat2_lib
)

add_test(NAME test_image_processing COMMAND test_image_processing)

set_tests_properties(test_image_processing PROPERTIES
	TIMEOUT 10
)

# Image create/destroy tests (task-069)
add_executable(test_image_create_destroy
	unit/main.c
	unit/test_image_create_destroy.c
)

target_link_libraries(test_image_create_destroy
	imgcat2_lib
)

add_test(NAME test_image_create_destroy COMMAND test_image_create_destroy)

set_tests_properties(test_image_create_destroy PROPERTIES
	TIMEOUT 10
)

# ============================================================================
# INTEGRATION TESTS
# ============================================================================

# Pipeline end-to-end tests (task-071)
add_executable(test_pipeline_end_to_end
	integration/main.c
	integration/test_pipeline_end_to_end.c
)

target_link_libraries(test_pipeline_end_to_end
	imgcat2_lib
)

add_test(NAME test_pipeline_end_to_end COMMAND test_pipeline_end_to_end)

set_tests_properties(test_pipeline_end_to_end PROPERTIES
	TIMEOUT 10
)

# CLI arguments tests (task-072)
add_executable(test_cli_arguments_integration
	integration/main.c
	integration/test_cli_arguments.c
)

target_link_libraries(test_cli_arguments_integration
	imgcat2_lib
)

add_test(NAME test_cli_arguments_integration COMMAND test_cli_arguments_integration)

set_tests_properties(test_cli_arguments_integration PROPERTIES
	TIMEOUT 10
)

# Animation tests (task-073)
add_executable(test_animation
	integration/main.c
	integration/test_animation.c
)

target_link_libraries(test_animation
	imgcat2_lib
)

add_test(NAME test_animation COMMAND test_animation)

set_tests_properties(test_animation PROPERTIES
	TIMEOUT 10
)

# Stdin pipe tests (task-074)
add_executable(test_stdin_pipe
	integration/main.c
	integration/test_stdin_pipe.c
)

target_link_libraries(test_stdin_pipe
	imgcat2_lib
)

add_test(NAME test_stdin_pipe COMMAND test_stdin_pipe)

set_tests_properties(test_stdin_pipe PROPERTIES
	TIMEOUT 10
)

# Cross-platform tests (task-075)
add_executable(test_cross_platform
	integration/main.c
	integration/test_cross_platform.c
)

target_link_libraries(test_cross_platform
	imgcat2_lib
)

add_test(NAME test_cross_platform COMMAND test_cross_platform)

set_tests_properties(test_cross_platform PROPERTIES
	TIMEOUT 10
)
