# Purpose: libtiff (optional): vendored or host.
# Inputs:  IMG2_ENABLE_TIFF, IMG2_USE_SYSTEM_TIFF, IMG2_USE_SYSTEM_JPEG, JPEG/WebP/zlib results.
# Outputs: TIFF_FOUND, TIFF_LIBRARIES, TIFF_INCLUDE_DIRS, TIFF_LIB_TYPE, img2_feature(TIFF).

# TIFF (optional)
if(IMG2_ENABLE_TIFF AND NOT IMG2_USE_SYSTEM_TIFF)
	# Vendored libtiff: codecs = built-ins + zlib (zlib-ng) + JPEG (vendored libjpeg-turbo)
	# + WebP (vendored libwebp when enabled). jbig (GPL), lerc, lzma, zstd, libdeflate,
	# C++ API, tools, tests, docs: OFF. libtiff pins cmake_policy(VERSION 3.10), so CMP0077 is unset there
	# and the vendor section's CMAKE_POLICY_DEFAULT_CMP0077 NEW makes its option() honour these normal variables.
	set(tiff-static ON)      # static
	set(tiff-tools OFF)      # no tools
	set(tiff-tests OFF)      # no tests
	set(tiff-contrib OFF)    # no contrib
	set(tiff-docs OFF)       # no docs
	set(tiff-install OFF)    # no install rules
	set(tiff-cxx OFF)        # no C++ stream API
	set(tiff-opengl OFF)     # no OpenGL viewer
	set(sphinx OFF)          # no sphinx docs
	set(zlib ON)             # Deflate via zlib-ng
	set(pixarlog ON)         # PixarLog (zlib)
	set(libdeflate OFF)      # no libdeflate
	set(jpeg ON)             # JPEG via vendored libjpeg-turbo
	set(old-jpeg ON)         # Old JPEG (read-only)
	set(jpeg12 OFF)          # no separate 12-bit libjpeg
	set(jbig OFF)            # no JBIG (GPL)
	set(lerc OFF)            # no LERC
	set(lzma OFF)            # no LZMA
	set(zstd OFF)            # no ZSTD
	if(WEBP_FOUND AND TARGET webp)
		set(webp ON)         # WebP via vendored libwebp
		# libtiff's own FindWebP: resolve to the vendored target, not a host libwebp
		# (normal variables, CMP0125 NEW: its find_* keep them)
		set(WebP_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/vendor/libwebp/src")
		set(WebP_LIBRARY webp)
		if(NOT TARGET WebP::webp)
			add_library(WebP::webp ALIAS webp)
		endif()
	else()
		set(webp OFF)        # no WebP
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
	# link at configure time because the vendored library is built later. A normal variable counts as
	# DEFINED, so libtiff's check_* macros skip the probe exactly as with a cache entry.
	if(TARGET libjpeg-turbo-ext)
		set(HAVE_JPEGTURBO_DUAL_MODE_8 1)    # jpeg_read_scanlines
		set(HAVE_JPEGTURBO_DUAL_MODE_12 1)   # jpeg12_read_scanlines
	endif()

	# libtiff's FindCMath would find_library(m) -> /usr/lib/.../libm.a (CMAKE_FIND_LIBRARY_SUFFIXES .a) and bake
	# that absolute static path into TIFF::tiff's link interface, which breaks dynamic (PIE) consumers. The plain
	# name links the same archive under -static and libm.so otherwise (normal variable, CMP0125 NEW).
	if(UNIX AND NOT APPLE)
		set(CMath_LIBRARY m)
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
