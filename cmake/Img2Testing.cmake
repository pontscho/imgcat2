# Purpose: Executable link flags and the test registration helper.
# Inputs:  IMG2_EXE_LINK_FLAGS, STATIC_LIBCXX_LIBS/DYNAMIC_LIBCXX_LIBS, IMG2_HAVE_<X>, targets img2_testing/imgcat2_cli
#          (and img2_fat when the fat archive is built).
# Outputs: functions img2_static_executable(<tgt> [STATIC_PIE]), img2_add_test(<name> ...).

# Executable link flags of today's fully static build (-static, stack size; macOS libc++).
# STATIC_PIE (Linux, caller has checked toolchain support): -static becomes -static-pie and the target's own
# sources are compiled with -fPIE ([F30]: ASLR for the main image; every linked archive must be PIC/PIE).
# Not the POSITION_INDEPENDENT_CODE property: on an executable CMake (CMP0083) then appends -pie to the link
# line after -static-pie, and the last one wins (a dynamic PIE with an interpreter).
function(img2_static_executable tgt)
	set(_flags ${IMG2_EXE_LINK_FLAGS})
	if("STATIC_PIE" IN_LIST ARGN)
		list(TRANSFORM _flags REPLACE "^-static$" "-static-pie")
		target_compile_options(${tgt} PRIVATE -fPIE)
	endif()
	target_link_options(${tgt} PRIVATE ${_flags})
	if(APPLE AND STATIC_LIBCXX_LIBS)
		target_link_libraries(${tgt} PRIVATE ${STATIC_LIBCXX_LIBS})
	elseif(APPLE AND DYNAMIC_LIBCXX_LIBS)
		target_link_libraries(${tgt} PRIVATE ${DYNAMIC_LIBCXX_LIBS})
	endif()
endfunction()

# img2_add_test(<name> SOURCES <files...> [KIND unit|integration] [LINK lib|cli]
#               [REQUIRES <FEATURE>...] [TIMEOUT <s>] [WORKDIR <dir>] [LABELS ...])
# KIND selects the test runner main (<KIND>/main.c); LINK cli adds the CLI library.
# The test is skipped when a REQUIRES feature is not built, or LINK cli without imgcat2_cli.
function(img2_add_test name)
	cmake_parse_arguments(T "" "KIND;LINK;TIMEOUT;WORKDIR" "SOURCES;REQUIRES;LABELS" ${ARGN})
	if(NOT T_KIND)
		set(T_KIND unit)
	endif()
	if(NOT T_LINK)
		set(T_LINK lib)
	endif()
	if(NOT T_TIMEOUT)
		set(T_TIMEOUT 10)
	endif()
	foreach(_f IN LISTS T_REQUIRES)
		if(NOT IMG2_HAVE_${_f})
			return()
		endif()
	endforeach()
	if(T_LINK STREQUAL "cli" AND NOT TARGET imgcat2_cli)
		return()
	endif()
	add_executable(${name} ${T_KIND}/main.c ${T_SOURCES})
	if(T_LINK STREQUAL "cli")
		target_link_libraries(${name} PRIVATE imgcat2_cli img2_testing)
	else()
		target_link_libraries(${name} PRIVATE img2_testing)
	endif()
	target_compile_definitions(${name} PRIVATE IMGCAT2_TEST_TMPDIR="${PROJECT_BINARY_DIR}/test-tmp")
	img2_apply_project_settings(${name})
	if(TARGET img2_fat)
		add_dependencies(${name} img2_fat)   # img2_testing links the fat archive (img2_bundled)
	endif()
	# Tests link dynamically (toolchain default, PIE on Linux): the first in-tree PIE consumers of img2
	add_test(NAME ${name} COMMAND ${name})
	if(NOT T_LABELS)
		set(T_LABELS ${T_KIND} ${T_LINK})
	endif()
	set_tests_properties(${name} PROPERTIES TIMEOUT ${T_TIMEOUT} LABELS "${T_LABELS}"
		ENVIRONMENT "TMPDIR=${PROJECT_BINARY_DIR}/test-tmp")
	if(T_WORKDIR)
		set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY ${T_WORKDIR})
	endif()
endfunction()
