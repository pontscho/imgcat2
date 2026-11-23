# Purpose: libwebp (optional): vendored or host.
# Inputs:  IMG2_ENABLE_WEBP, IMG2_USE_SYSTEM_WEBP.
# Outputs: WEBP_FOUND, WEBP_LIBRARIES, WEBP_INCLUDE_DIRS, WEBP_LIB_TYPE, img2_feature(WEBP).

# WebP (optional)
if(IMG2_ENABLE_WEBP AND NOT IMG2_USE_SYSTEM_WEBP)
	# Vendored libwebp: only the libraries (webpmux, webpdemux, webp, sharpyuv), no tools/extras
	# Knobs as normal variables (vendor section: CMAKE_POLICY_DEFAULT_CMP0077 NEW)
	set(WEBP_LINK_STATIC ON)          # static
	set(WEBP_BUILD_ANIM_UTILS OFF)    # no anim utils
	set(WEBP_BUILD_CWEBP OFF)         # no cwebp
	set(WEBP_BUILD_DWEBP OFF)         # no dwebp
	set(WEBP_BUILD_GIF2WEBP OFF)      # no gif2webp
	set(WEBP_BUILD_IMG2WEBP OFF)      # no img2webp
	set(WEBP_BUILD_VWEBP OFF)         # no vwebp
	set(WEBP_BUILD_WEBPINFO OFF)      # no webpinfo
	set(WEBP_BUILD_LIBWEBPMUX ON)     # libwebpmux: EXIF / XMP chunks of the WebP encoder (metadata passthrough)
	set(WEBP_BUILD_WEBPMUX OFF)       # no webpmux tool
	set(WEBP_BUILD_EXTRAS OFF)        # no extras
	set(WEBP_BUILD_FUZZTEST OFF)      # no fuzztest
	imgcat2_vendor_begin()
	add_subdirectory(vendor/libwebp EXCLUDE_FROM_ALL)
	imgcat2_vendor_end()
	set(WEBP_FOUND TRUE)
	set(WEBP_LIBRARIES libwebpmux webpdemux webp sharpyuv)
	set(WEBP_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/vendor/libwebp/src")
	set(WEBP_LIB_TYPE "vendored libwebp 1.6.0")
	img2_feature(WEBP LIBS ${WEBP_LIBRARIES} INCLUDE_DIRS ${WEBP_INCLUDE_DIRS}
		ARCHIVES "$<TARGET_FILE:libwebpmux>" "$<TARGET_FILE:webpdemux>" "$<TARGET_FILE:webp>" "$<TARGET_FILE:sharpyuv>"
		ARCHIVE_TARGETS libwebpmux webpdemux webp sharpyuv)

elseif(IMG2_ENABLE_WEBP)
	set(WEBP_LIB_TYPE "host")
	# Find static library explicitly on macOS
	if(APPLE)
		find_library(WEBP_LIBRARY NAMES webp)
		find_library(WEBPDEMUX_LIBRARY NAMES webpdemux)
		find_library(WEBPMUX_LIBRARY NAMES webpmux)
		find_path(WEBP_INCLUDE_DIR webp/decode.h)
		if(WEBP_LIBRARY AND WEBPDEMUX_LIBRARY AND WEBPMUX_LIBRARY AND WEBP_INCLUDE_DIR)
			set(WEBP_LIBRARIES ${WEBPMUX_LIBRARY} ${WEBP_LIBRARY} ${WEBPDEMUX_LIBRARY})
			set(WEBP_INCLUDE_DIRS ${WEBP_INCLUDE_DIR})
			set(WEBP_FOUND TRUE)
		endif()

	else()
		# libwebpmux: EXIF / XMP chunks of the WebP encoder (metadata passthrough); shipped with libwebp
		pkg_check_modules(WEBP libwebp libwebpdemux libwebpmux)
	endif()

	if(WEBP_FOUND)
		img2_feature(WEBP LIBS ${WEBP_LIBRARIES} INCLUDE_DIRS ${WEBP_INCLUDE_DIRS} SYSTEM_LIBS ${WEBP_LIBRARIES})
		img2_warn_host_dependency("libwebp (IMG2_USE_SYSTEM_WEBP)")

	else()
		message(WARNING "libwebp not found, WebP support disabled")
		set(IMG2_ENABLE_WEBP OFF)
	endif()
endif()
