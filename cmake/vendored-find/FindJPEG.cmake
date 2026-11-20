# imgcat2 shim (see README.md): libheif's JPEG codec uses the vendored libjpeg-turbo
# (JPEG::JPEG imported target, which carries the build-order dependency on the
# libjpeg-turbo ExternalProject). With USE_SYSTEM_JPEG=ON, CMake's own module is used.
if(IMGCAT2_VENDORED_JPEG_LIBRARIES)
	set(JPEG_FOUND TRUE)
	set(JPEG_LIBRARIES ${IMGCAT2_VENDORED_JPEG_LIBRARIES})
	set(JPEG_INCLUDE_DIRS ${IMGCAT2_VENDORED_JPEG_INCLUDE_DIRS})
else()
	include("${CMAKE_ROOT}/Modules/FindJPEG.cmake")
endif()
