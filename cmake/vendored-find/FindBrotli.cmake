# imgcat2 shim (see README.md): the single vendored Brotli, shared by libheif and libjxl.
# libheif reads BROTLI_INCLUDE_DIRS / BROTLI_LIBS, libjxl links the brotlicommon /
# brotlidec / brotlienc targets by name (they exist: vendor/brotli is added first).
if(IMGCAT2_VENDORED_BROTLI_LIBRARIES)
	set(Brotli_FOUND TRUE)
	set(BROTLI_FOUND TRUE)
	set(HAVE_BROTLI TRUE)
	set(BROTLI_INCLUDE_DIR ${IMGCAT2_VENDORED_BROTLI_INCLUDE_DIRS})
	set(BROTLI_INCLUDE_DIRS ${IMGCAT2_VENDORED_BROTLI_INCLUDE_DIRS})
	set(Brotli_INCLUDE_DIRS ${IMGCAT2_VENDORED_BROTLI_INCLUDE_DIRS})
	set(BROTLI_LIBS ${IMGCAT2_VENDORED_BROTLI_LIBRARIES})
	set(Brotli_LIBRARIES ${IMGCAT2_VENDORED_BROTLI_LIBRARIES})
else()
	set(Brotli_FOUND FALSE)
	set(BROTLI_FOUND FALSE)
	set(HAVE_BROTLI FALSE)
endif()
