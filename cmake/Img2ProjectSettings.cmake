# Purpose: Project-only compile settings applied after the vendored dependencies are declared.
# Inputs:  IMGCAT2_WARNING_FLAGS, IMGCAT2_CLANG_TIDY.
# Outputs: Directory compile options (warnings), CMAKE_C_CLANG_TIDY, CMAKE_<LANG>_IMPLICIT_LINK_LIBRARIES without gcc_s.

# Project-only compile settings
# Everything declared above (vendored dependencies) is created without these; every
# target created below (imgcat2, imgcat2_lib, src/tests) gets -Werror and clang-tidy.
add_compile_options(${IMGCAT2_WARNING_FLAGS})
if(IMGCAT2_CLANG_TIDY)
	set(CMAKE_C_CLANG_TIDY "${IMGCAT2_CLANG_TIDY}")
endif()

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
