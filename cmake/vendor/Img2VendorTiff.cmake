# Purpose: libtiff (optional): vendored or host.
# Inputs:  IMG2_ENABLE_TIFF, IMG2_USE_SYSTEM_TIFF, IMG2_USE_SYSTEM_JPEG, JPEG/WebP/zlib results.
# Outputs: TIFF_FOUND, TIFF_LIBRARIES, TIFF_INCLUDE_DIRS, TIFF_LIB_TYPE, img2_feature(TIFF).

# TIFF (optional)
if(IMG2_ENABLE_TIFF AND NOT IMG2_USE_SYSTEM_TIFF)
	# Vendored libtiff: codecs = built-ins + zlib (zlib-ng) + JPEG (vendored libjpeg-turbo)
	# + WebP (vendored libwebp when enabled). jbig (GPL), lerc, lzma, zstd, libdeflate,
	# C++ API, tools, tests, docs: OFF. libtiff pins cmake_policy(VERSION 3.10), so its
	# options are pre-seeded as CACHE entries (CMP0077 OLD ignores normal variables).
	set(tiff-static ON CACHE BOOL "libtiff: static")
	set(tiff-tools OFF CACHE BOOL "libtiff: no tools")
	set(tiff-tests OFF CACHE BOOL "libtiff: no tests")
	set(tiff-contrib OFF CACHE BOOL "libtiff: no contrib")
	set(tiff-docs OFF CACHE BOOL "libtiff: no docs")
	set(tiff-install OFF CACHE BOOL "libtiff: no install rules")
	set(tiff-cxx OFF CACHE BOOL "libtiff: no C++ stream API")
	set(tiff-opengl OFF CACHE BOOL "libtiff: no OpenGL viewer")
	set(sphinx OFF CACHE BOOL "libtiff: no sphinx docs")
	set(zlib ON CACHE BOOL "libtiff: Deflate via zlib-ng")
	set(pixarlog ON CACHE BOOL "libtiff: PixarLog (zlib)")
	set(libdeflate OFF CACHE BOOL "libtiff: no libdeflate")
	set(jpeg ON CACHE BOOL "libtiff: JPEG via vendored libjpeg-turbo")
	set(old-jpeg ON CACHE BOOL "libtiff: Old JPEG (read-only)")
	set(jpeg12 OFF CACHE BOOL "libtiff: no separate 12-bit libjpeg")
	set(jbig OFF CACHE BOOL "libtiff: no JBIG (GPL)")
	set(lerc OFF CACHE BOOL "libtiff: no LERC")
	set(lzma OFF CACHE BOOL "libtiff: no LZMA")
	set(zstd OFF CACHE BOOL "libtiff: no ZSTD")
	if(WEBP_FOUND AND TARGET webp)
		set(webp ON CACHE BOOL "libtiff: WebP via vendored libwebp")
		# libtiff's own FindWebP: resolve to the vendored target, not a host libwebp
		set(WebP_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/vendor/libwebp/src" CACHE PATH "libwebp include dir (vendored)" FORCE)
		set(WebP_LIBRARY webp CACHE STRING "libwebp library (vendored target)" FORCE)
		if(NOT TARGET WebP::webp)
			add_library(WebP::webp ALIAS webp)
		endif()
	else()
		set(webp OFF CACHE BOOL "libtiff: no WebP")
	endif()

	# libtiff links JPEG::JPEG by name; the host pkg-config path (IMG2_USE_SYSTEM_JPEG) has none
	if(NOT TARGET JPEG::JPEG)
		add_library(JPEG::JPEG INTERFACE IMPORTED GLOBAL)
		if(JPEG_LINK_LIBRARIES)
			set(_tiff_jpeg_libs ${JPEG_LINK_LIBRARIES})
		else()
			set(_tiff_jpeg_libs ${JPEG_LIBRARIES})
		endif()
		set_target_properties(JPEG::JPEG PROPERTIES
			INTERFACE_LINK_LIBRARIES "${_tiff_jpeg_libs}"
			INTERFACE_INCLUDE_DIRECTORIES "${JPEG_INCLUDE_DIRS}")
	endif()

	# libjpeg-turbo >= 3.0 always provides the 8/12-bit dual-mode API; the probe cannot
	# link at configure time because the vendored library is built later
	if(TARGET libjpeg-turbo-ext)
		set(HAVE_JPEGTURBO_DUAL_MODE_8 1 CACHE INTERNAL "vendored libjpeg-turbo: jpeg_read_scanlines")
		set(HAVE_JPEGTURBO_DUAL_MODE_12 1 CACHE INTERNAL "vendored libjpeg-turbo: jpeg12_read_scanlines")
	endif()

	# Keep libtiff's optional find_package() probes away from host libraries
	foreach(_pkg libjpeg-turbo Deflate JBIG LERC liblzma ZSTD OpenGL GLUT)
		set(CMAKE_DISABLE_FIND_PACKAGE_${_pkg} TRUE)
	endforeach()
	imgcat2_vendor_begin()
	add_subdirectory(vendor/libtiff EXCLUDE_FROM_ALL)
	imgcat2_vendor_end()
	foreach(_pkg libjpeg-turbo Deflate JBIG LERC liblzma ZSTD OpenGL GLUT)
		unset(CMAKE_DISABLE_FIND_PACKAGE_${_pkg})
	endforeach()

	set(TIFF_FOUND TRUE)
	set(TIFF_LIBRARIES TIFF::tiff)
	set(TIFF_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/vendor/libtiff/libtiff" "${PROJECT_BINARY_DIR}/vendor/libtiff/libtiff")
	set(TIFF_LIB_TYPE "vendored libtiff 4.7.2")
	img2_feature(TIFF LIBS ${TIFF_LIBRARIES} INCLUDE_DIRS ${TIFF_INCLUDE_DIRS})

elseif(IMG2_ENABLE_TIFF)
	set(TIFF_LIB_TYPE "host")
	pkg_check_modules(TIFF libtiff-4)
	if(NOT TIFF_FOUND)
		pkg_check_modules(TIFF libtiff)
	endif()
	if(TIFF_FOUND)
		# On macOS, explicitly find static library and its dependencies
		if(APPLE)
			find_library(TIFF_STATIC_LIB NAMES libtiff.a REQUIRED)
			set(TIFF_CUSTOM_LIBS ${TIFF_STATIC_LIB})

			# Add TIFF dependencies
			find_library(ZSTD_STATIC NAMES libzstd.a)
			find_library(LZMA_STATIC NAMES liblzma.a)
			if(ZSTD_STATIC)
				list(APPEND TIFF_CUSTOM_LIBS ${ZSTD_STATIC})
			endif()
			if(LZMA_STATIC)
				list(APPEND TIFF_CUSTOM_LIBS ${LZMA_STATIC})
			endif()
		else()
			imgcat2_strip_vendored_ldflags(TIFF_STATIC_LDFLAGS)
		endif()
		if(APPLE)
			img2_feature(TIFF LIBS ${TIFF_CUSTOM_LIBS} INCLUDE_DIRS ${TIFF_INCLUDE_DIRS})
		else()
			img2_feature(TIFF LIBS ${TIFF_STATIC_LDFLAGS} INCLUDE_DIRS ${TIFF_INCLUDE_DIRS})
		endif()
	else()
		message(WARNING "libtiff not found, TIFF support disabled")
		set(IMG2_ENABLE_TIFF OFF)
	endif()
endif()
