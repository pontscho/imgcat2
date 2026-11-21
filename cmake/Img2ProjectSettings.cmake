# Purpose: Project-only compile settings, applied per target (vendored dependencies never get them).
# Inputs:  IMGCAT2_WARNING_FLAGS, IMGCAT2_CLANG_TIDY.
# Outputs: function img2_apply_project_settings(<tgt>), CMAKE_<LANG>_IMPLICIT_LINK_LIBRARIES without gcc_s.

# Project-only compile settings: warnings (-Werror) and clang-tidy go on our own targets only
# (img2, imgcat2_cli, imgcat2, tests); vendored targets are created without them.
function(img2_apply_project_settings tgt)
	target_compile_options(${tgt} PRIVATE ${IMGCAT2_WARNING_FLAGS})
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
