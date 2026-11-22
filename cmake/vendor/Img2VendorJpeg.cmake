# Purpose: libjpeg-turbo (required): vendored via ExternalProject or host.
# Inputs:  IMG2_USE_SYSTEM_JPEG.
# Outputs: JPEG::JPEG, JPEG_FOUND, JPEG_LIBRARIES, JPEG_INCLUDE_DIRS, JPEG_LIB_TYPE, LIBJPEG_TURBO_LIBRARY, img2_feature(LIBJPEG).

# libjpeg-turbo (REQUIRED)
if(IMG2_USE_SYSTEM_JPEG)
	# Host libjpeg-turbo (preferred) or libjpeg
	pkg_check_modules(JPEG libjpeg-turbo)
	if(NOT JPEG_FOUND)
		find_package(JPEG)
	endif()

	if(NOT JPEG_FOUND)
		message(FATAL_ERROR "libjpeg or libjpeg-turbo is required but not found")
	endif()
	link_directories(${JPEG_LIBRARY_DIRS})
	set(JPEG_LIB_TYPE "host")
	set(_jpeg_feature_args SYSTEM_LIBS ${JPEG_LIBRARIES})
	img2_warn_host_dependency("libjpeg (IMG2_USE_SYSTEM_JPEG)")

else()
	# Vendored libjpeg-turbo. Upstream refuses add_subdirectory() (FATAL_ERROR in its
	# CMakeLists.txt), so it is built with ExternalProject_Add and exposed as an
	# IMPORTED target. WITH_JPEG8: libjpeg v8 API/ABI (JPEG_LIB_VERSION 80), because the
	# host static libheif.a was built against the jpeg8 ABI and resolves jpeg_* from this
	# library. Only the libjpeg API is used by imgcat2 (no tj* calls) -> no turbojpeg.
	include(ExternalProject)
	set(LIBJPEG_TURBO_PREFIX "${PROJECT_BINARY_DIR}/_deps/libjpeg-turbo-install")
	set(LIBJPEG_TURBO_LIBRARY "${LIBJPEG_TURBO_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}jpeg${CMAKE_STATIC_LIBRARY_SUFFIX}")
	set(_jpeg_cache_args
		-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}
		-DCMAKE_BUILD_TYPE:STRING=Release
		-DCMAKE_INSTALL_PREFIX:PATH=${LIBJPEG_TURBO_PREFIX}
		-DCMAKE_INSTALL_LIBDIR:PATH=lib
		-DCMAKE_INSTALL_INCLUDEDIR:PATH=include
		-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=${IMG2_POSITION_INDEPENDENT_CODE}
		-DENABLE_SHARED:BOOL=OFF
		-DENABLE_STATIC:BOOL=ON
		-DWITH_JPEG8:BOOL=ON
		-DWITH_TURBOJPEG:BOOL=OFF
		-DWITH_TOOLS:BOOL=OFF
		-DWITH_TESTS:BOOL=OFF
	)
	if(IMG2_HARDENING_FLAGS_STRING)
		# [F32] hardening flags of the vendor section (C only; the NASM SIMD flags stay upstream's). CMAKE_C_FLAGS
		# is empty otherwise, and CMAKE_C_FLAGS_RELEASE (-O3 -DNDEBUG) is left to upstream.
		list(APPEND _jpeg_cache_args "-DCMAKE_C_FLAGS:STRING=${IMG2_HARDENING_FLAGS_STRING}")
	endif()
	if(APPLE)
		list(APPEND _jpeg_cache_args
			"-DCMAKE_OSX_ARCHITECTURES:STRING=${CMAKE_OSX_ARCHITECTURES}"
			"-DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=${CMAKE_OSX_DEPLOYMENT_TARGET}"
			"-DCMAKE_OSX_SYSROOT:PATH=${CMAKE_OSX_SYSROOT}")
	endif()
	ExternalProject_Add(libjpeg-turbo-ext
		SOURCE_DIR "${PROJECT_SOURCE_DIR}/vendor/libjpeg-turbo"
		BINARY_DIR "${PROJECT_BINARY_DIR}/_deps/libjpeg-turbo-build"
		INSTALL_DIR "${LIBJPEG_TURBO_PREFIX}"
		CMAKE_CACHE_ARGS ${_jpeg_cache_args}
		BUILD_BYPRODUCTS "${LIBJPEG_TURBO_LIBRARY}"
		UPDATE_COMMAND ""
		EXCLUDE_FROM_ALL TRUE
	)
	# Headers are installed at build time; the directory must exist at generate time
	file(MAKE_DIRECTORY "${LIBJPEG_TURBO_PREFIX}/include")
	if(NOT TARGET JPEG::JPEG)   # never overwrite the properties of an existing JPEG::JPEG
		add_library(JPEG::JPEG STATIC IMPORTED GLOBAL)
		set_target_properties(JPEG::JPEG PROPERTIES
			IMPORTED_LOCATION "${LIBJPEG_TURBO_LIBRARY}"
			INTERFACE_INCLUDE_DIRECTORIES "${LIBJPEG_TURBO_PREFIX}/include")
		add_dependencies(JPEG::JPEG libjpeg-turbo-ext)
	endif()

	set(JPEG_FOUND TRUE)
	set(JPEG_LIBRARIES JPEG::JPEG)
	set(JPEG_INCLUDE_DIRS "${LIBJPEG_TURBO_PREFIX}/include")
	# Pre-seed FindJPEG for vendored consumers (libtiff) calling find_package(JPEG)
	# (normal variables, CMP0125 NEW: FindJPEG's find_* keep them)
	set(JPEG_INCLUDE_DIR "${LIBJPEG_TURBO_PREFIX}/include")
	set(JPEG_LIBRARY "${LIBJPEG_TURBO_LIBRARY}")
	set(JPEG_LIB_TYPE "vendored libjpeg-turbo 3.2.1, jpeg8 ABI")
	set(_jpeg_feature_args ARCHIVES "${LIBJPEG_TURBO_LIBRARY}" ARCHIVE_TARGETS libjpeg-turbo-ext)
endif()
img2_feature(LIBJPEG LIBS ${JPEG_LIBRARIES} INCLUDE_DIRS ${JPEG_INCLUDE_DIRS} ${_jpeg_feature_args})
