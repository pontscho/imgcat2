# Purpose: Vendored providers linked after every host static library (static link order).
# Inputs:  Vendored targets (libjpeg-turbo-ext, webp, vendor_lcms2, brotlicommon, ZLIB::ZLIB), LIBJPEG_TURBO_LIBRARY, WEBP_FOUND.
# Outputs: IMGCAT2_VENDORED_TRAILING_LIBS.

# Vendored providers linked after every host static library (libheif stack, libjxl, and
# libtiff/libraw when USE_SYSTEM_*): those reference jpeg/webp/lcms2/zlib symbols, and with
# static linking the provider must come after its user on the link line.
# Archive paths (not target names) on purpose: CMake de-duplicates targets in the
# transitive link closure (img2 -> imgcat2_cli -> tests) and would drop the trailing copy, while
# raw items are kept in place. Build order is already ensured by the target links above.
set(IMGCAT2_VENDORED_TRAILING_LIBS "")
if(TARGET libjpeg-turbo-ext)
	list(APPEND IMGCAT2_VENDORED_TRAILING_LIBS "${LIBJPEG_TURBO_LIBRARY}")
endif()
if(TARGET webp AND WEBP_FOUND)
	list(APPEND IMGCAT2_VENDORED_TRAILING_LIBS
		"$<TARGET_FILE:webpdemux>" "$<TARGET_FILE:webp>" "$<TARGET_FILE:sharpyuv>")
endif()
if(TARGET vendor_lcms2)
	list(APPEND IMGCAT2_VENDORED_TRAILING_LIBS "$<TARGET_FILE:vendor_lcms2>")
endif()
if(TARGET brotlicommon)
	# Host libheif (IMG2_USE_SYSTEM_HEIF) next to the brotli vendored for libjxl
	list(APPEND IMGCAT2_VENDORED_TRAILING_LIBS
		"$<TARGET_FILE:brotlienc>" "$<TARGET_FILE:brotlidec>" "$<TARGET_FILE:brotlicommon>")
endif()
list(APPEND IMGCAT2_VENDORED_TRAILING_LIBS "$<TARGET_FILE:ZLIB::ZLIB>")
