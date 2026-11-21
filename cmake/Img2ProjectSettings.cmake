# Purpose: Project-only compile settings, applied per target (vendored dependencies never get them).
# Inputs:  IMGCAT2_WARNING_FLAGS, IMGCAT2_CLANG_TIDY.
# Outputs: function img2_apply_project_settings(<tgt>), IMG2_HARDENING_FLAGS, IMG2_FORTIFY_PREDEFINED,
#          CMAKE_<LANG>_IMPLICIT_LINK_LIBRARIES without gcc_s.

# [SL2] Hardening flags for our own sources (never vendors). _FORTIFY_SOURCE is added only when the toolchain
# does not predefine it at our optimisation level (Ubuntu's GCC does; redefining it would trip -Werror).
set(IMG2_HARDENING_FLAGS "")
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
	include(CheckCSourceCompiles)
	include(CMakePushCheckState)
	cmake_push_check_state(RESET)
	set(CMAKE_REQUIRED_FLAGS -O3)
	check_c_source_compiles("#ifndef _FORTIFY_SOURCE\n#error not predefined\n#endif\nint main(void){return 0;}"
		IMG2_FORTIFY_PREDEFINED)
	cmake_pop_check_state()
	list(APPEND IMG2_HARDENING_FLAGS -fstack-protector-strong)
	if(NOT IMG2_FORTIFY_PREDEFINED)
		list(APPEND IMG2_HARDENING_FLAGS -D_FORTIFY_SOURCE=2)   # -O3 is always on for our targets
	endif()
endif()

# Project-only compile settings: -O3 -g, warnings (-Werror with IMG2_WERROR), hardening and clang-tidy go on
# our own targets only (img2, imgcat2_cli, imgcat2, tests); vendored targets are created without them
# (they get -O3 -g from the vendor section), and nothing is global, so consumer targets see none of them.
function(img2_apply_project_settings tgt)
	target_compile_options(${tgt} PRIVATE -O3 -g ${IMGCAT2_WARNING_FLAGS} ${IMG2_HARDENING_FLAGS})
	if(IMGCAT2_CLANG_TIDY)
		set_target_properties(${tgt} PROPERTIES C_CLANG_TIDY "${IMGCAT2_CLANG_TIDY}")
	endif()
endfunction()

# Fully static Linux link: when a C++ dependency (vendored LibRaw) makes C++ the linker
# language, CMake appends the C compiler's implicit libs, which include the shared-only
# libgcc_s (no libgcc_s.a exists). The compiler driver handles libgcc itself under -static.
if(UNIX AND NOT APPLE)
	foreach(_lang C CXX)
		if(CMAKE_${_lang}_IMPLICIT_LINK_LIBRARIES)
			list(REMOVE_ITEM CMAKE_${_lang}_IMPLICIT_LINK_LIBRARIES gcc_s)
		endif()
	endforeach()
endif()
