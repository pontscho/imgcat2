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
