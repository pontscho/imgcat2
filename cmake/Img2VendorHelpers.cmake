# Purpose: Helpers for vendored subprojects (test/install isolation, host ldflag stripping).
# Inputs:  BUILD_TESTING, vendored targets (ZLIB::ZLIB, png_static, webp, ...) at call time.
# Outputs: macros imgcat2_vendor_begin/imgcat2_vendor_end, function imgcat2_strip_vendored_ldflags().

# Helper for vendored add_subdirectory() projects: their own tests/tools must never
# register with our CTest or build by default.
# imgcat2_vendor_begin(NO_INSTALL) additionally skips the install rules of the
# subproject (directory-scoped CMAKE_SKIP_INSTALL_RULES): upstream install(EXPORT)
# sets would otherwise require every vendored dependency they link (zlib-ng, brotli,
# kvazaar, ...) to be in an export set too. imgcat2's own install rules are unaffected.
macro(imgcat2_vendor_begin)
	set(_IMGCAT2_SAVED_BUILD_TESTING ${BUILD_TESTING})
	set(BUILD_TESTING OFF)
	# Macro arguments are not variables: copy ARGN before IN_LIST
	set(_imgcat2_vendor_args ${ARGN})
	if("NO_INSTALL" IN_LIST _imgcat2_vendor_args)
		set(CMAKE_SKIP_INSTALL_RULES ON)
	endif()
endmacro()
macro(imgcat2_vendor_end)
	set(BUILD_TESTING ${_IMGCAT2_SAVED_BUILD_TESTING})
	unset(CMAKE_SKIP_INSTALL_RULES)
endmacro()

# Host static link flags (pkg-config --static) of libraries that still come from the
# host may name dependencies that are vendored here (-ljpeg, -lwebp, -lz, ...). Linking
# the host copies next to the vendored ones gives duplicate symbols / mixed ABIs, so
# they are dropped; the vendored providers are linked last instead
# (IMGCAT2_VENDORED_TRAILING_LIBS, after all host libraries).
function(imgcat2_strip_vendored_ldflags _var)
	set(_out "")
	foreach(_f IN LISTS ${_var})
		if((TARGET ZLIB::ZLIB AND _f STREQUAL "-lz")
			OR (TARGET libjpeg-turbo-ext AND _f MATCHES "^-l(jpeg|turbojpeg)$")
			OR (TARGET png_static AND _f MATCHES "^-lpng(16)?$")
			OR (TARGET vendor_giflib AND _f STREQUAL "-lgif")
			OR (TARGET webp AND _f MATCHES "^-l(webp|webpdemux|webpmux|sharpyuv)$")
			OR (TARGET vendor_lcms2 AND _f STREQUAL "-llcms2")
			OR (TARGET brotlicommon AND _f MATCHES "^-lbrotli(common|dec|enc)$")
			OR (TARGET hwy AND _f STREQUAL "-lhwy"))
			continue()
		endif()
		list(APPEND _out "${_f}")
	endforeach()
	set(${_var} "${_out}" PARENT_SCOPE)
endfunction()
