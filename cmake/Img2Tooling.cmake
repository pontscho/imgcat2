# Purpose: Developer tooling: clang-tidy / clang-format detection and the 'format' target.
# Inputs:  IMG2_ENABLE_CLANG_TIDY, IMG2_ENABLE_CLANG_FORMAT, PROJECT_SOURCE_DIR.
# Outputs: CLANG_TIDY_EXE, CLANG_FORMAT_EXE, IMGCAT2_CLANG_TIDY, function img2_add_format_target().

# clang-tidy detection
find_program(CLANG_TIDY_EXE NAMES clang-tidy)
if(IMG2_ENABLE_CLANG_TIDY)
	if(CLANG_TIDY_EXE)
		# Applied as CMAKE_C_CLANG_TIDY only after the vendored dependencies are
		# declared (see "Project-only compile settings"), so vendor code is not analyzed
		set(IMGCAT2_CLANG_TIDY "${CLANG_TIDY_EXE}")
		message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")

	else()
		message(STATUS "clang-tidy not found, static analysis disabled")
		set(IMG2_ENABLE_CLANG_TIDY OFF)
	endif()
endif()

# clang-format detection
find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(IMG2_ENABLE_CLANG_FORMAT)
	if(CLANG_FORMAT_EXE)
		message(STATUS "clang-format found: ${CLANG_FORMAT_EXE}")

	else()
		message(STATUS "clang-format not found, code formatting disabled")
		set(IMG2_ENABLE_CLANG_FORMAT OFF)
	endif()
endif()

# clang-format target (called by the top-level CMakeLists.txt after all targets exist)
function(img2_add_format_target)
	if(IMG2_ENABLE_CLANG_FORMAT AND CLANG_FORMAT_EXE)
		file(GLOB_RECURSE ALL_SOURCE_FILES
			${PROJECT_SOURCE_DIR}/src/*.c
			${PROJECT_SOURCE_DIR}/src/*.h
		)
		list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX "/src/imgcat2/compat/")
		add_custom_target(
			format
			COMMAND ${CLANG_FORMAT_EXE}
			-i
			-style=file
			${ALL_SOURCE_FILES}
			COMMENT "Running clang-format on all source files"
		)
	endif()
endfunction()
