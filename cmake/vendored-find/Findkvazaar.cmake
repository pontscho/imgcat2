# imgcat2 shim (see README.md): vendored kvazaar for libheif
# Feature probes of libheif's own Findkvazaar.cmake, answered statically for the
# pinned kvazaar release: kvz_config has enable_logging_output, there is no
# kvz_get_version_string().
if(IMGCAT2_VENDORED_KVAZAAR_LIBRARIES)
	set(KVAZAAR_FOUND TRUE)
	set(kvazaar_FOUND TRUE)
	set(KVAZAAR_LIBRARIES ${IMGCAT2_VENDORED_KVAZAAR_LIBRARIES})
	set(KVAZAAR_INCLUDE_DIRS ${IMGCAT2_VENDORED_KVAZAAR_INCLUDE_DIRS})
	set(KVAZAAR_INCLUDE_DIR ${IMGCAT2_VENDORED_KVAZAAR_INCLUDE_DIRS})
	set(HAVE_KVAZAAR_ENABLE_LOGGING 1)
	set(HAVE_KVAZAAR_VERSION_STRING 0)
else()
	set(KVAZAAR_FOUND FALSE)
	set(kvazaar_FOUND FALSE)
endif()
