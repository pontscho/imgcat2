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
