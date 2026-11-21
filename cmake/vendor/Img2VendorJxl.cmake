# Purpose: libjxl (optional): vendored libjxl + highway, or host.
# Inputs:  IMG2_ENABLE_JXL, IMG2_USE_SYSTEM_JXL, brotli/lcms2 results.
# Outputs: JXL_FOUND, JXL_LIBRARIES, JXL_INCLUDE_DIRS, JXL_LIB_TYPE, JXL_VERSION, img2_feature(JXL).

# libjxl (optional)
if(IMG2_ENABLE_JXL AND NOT IMG2_USE_SYSTEM_JXL)
	# Vendored libjxl 0.13.0 (BSD-3-Clause) on top-level vendored dependencies only:
	#   Highway 1.4.0 (vendor/highway, Apache-2.0 OR BSD-3-Clause), the shared vendored
	#   brotli and lcms2 (JPEGXL_ENABLE_SKCMS=OFF: lcms2 is the CMS, no skcms). libjxl's own
	#   third_party/ submodules (highway, brotli, lcms, skcms, libpng, zlib, libjpeg-turbo,
	#   googletest, sjpeg) are NOT initialized; the JPEGXL_FORCE_SYSTEM_* switches plus the
	#   cmake/vendored-find shims resolve them to the vendored targets.
	enable_language(CXX)
	set(IMGCAT2_VENDORED_FIND_DIR "${PROJECT_SOURCE_DIR}/cmake/vendored-find")

	# --- Highway (SIMD dispatch), static library only
	# (knobs here and below as normal variables; vendor section: CMAKE_POLICY_DEFAULT_CMP0077 NEW)
	set(HWY_FORCE_STATIC_LIBS ON)    # static library
	set(HWY_ENABLE_CONTRIB OFF)      # no contrib
	set(HWY_ENABLE_EXAMPLES OFF)     # no examples
	set(HWY_ENABLE_INSTALL OFF)      # no install rules
	set(HWY_ENABLE_TESTS OFF)        # no tests
	imgcat2_vendor_begin(NO_INSTALL)
	add_subdirectory(vendor/highway EXCLUDE_FROM_ALL)
	imgcat2_vendor_end()
	file(STRINGS "${PROJECT_SOURCE_DIR}/vendor/highway/hwy/base.h" _hwy_version_lines
		REGEX "^#define HWY_(MAJOR|MINOR|PATCH) +[0-9]+")
	set(_hwy_version "")
	foreach(_part MAJOR MINOR PATCH)
		string(REGEX MATCH "HWY_${_part} +([0-9]+)" _m "${_hwy_version_lines}")
		list(APPEND _hwy_version "${CMAKE_MATCH_1}")
	endforeach()
	string(JOIN "." IMGCAT2_VENDORED_HWY_VERSION ${_hwy_version})
	set(IMGCAT2_VENDORED_HWY_LIBRARIES hwy)
	set(IMGCAT2_VENDORED_HWY_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/vendor/highway")

	# --- lcms2 for jxl_cms: libjxl links a target literally named "lcms2"
	if(NOT TARGET lcms2)
		add_library(lcms2 ALIAS ${LCMS2_TARGET})
	endif()
	set(IMGCAT2_VENDORED_LCMS2_LIBRARIES ${LCMS2_TARGET})
	if(TARGET vendor_lcms2)
		set(IMGCAT2_VENDORED_LCMS2_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/vendor/lcms2/include")
		set(IMGCAT2_VENDORED_LCMS2_VERSION "2.20")
	else()
		set(IMGCAT2_VENDORED_LCMS2_INCLUDE_DIRS "${LCMS2_INCLUDE_DIRS}")
		set(IMGCAT2_VENDORED_LCMS2_VERSION "${LCMS2_VERSION}")
	endif()

	# --- libjxl: libraries only (jxl, jxl_cms; jxl_threads is built but unused)
	set(JPEGXL_FORCE_SYSTEM_HWY ON)       # top-level vendored highway
	set(JPEGXL_FORCE_SYSTEM_BROTLI ON)    # top-level vendored brotli
	set(JPEGXL_FORCE_SYSTEM_LCMS2 ON)     # top-level vendored lcms2
	set(JPEGXL_ENABLE_SKCMS OFF)          # lcms2 instead of skcms
	set(JPEGXL_BUNDLE_LIBPNG OFF)         # no bundled libpng
	foreach(_jxl_opt TOOLS DEVTOOLS EXAMPLES BENCHMARK FUZZERS DOXYGEN MANPAGES JNI SJPEG
			OPENEXR VIEWERS PLUGINS TCMALLOC COVERAGE)
		set(JPEGXL_ENABLE_${_jxl_opt} OFF)
	endforeach()
	set(JPEGXL_ENABLE_TRANSCODE_JPEG ON)  # JPEG reconstruction
	set(JPEGXL_ENABLE_BOXES ON)           # box (Exif/XMP) decoding
	set(JPEGXL_STATIC OFF)                # no static tool binaries
	set(JPEGXL_TEST_TOOLS OFF)            # no tool tests
	# The extras/tools find_package() probes are off with the tools; keep any left-over
	# probe away from host packages
	foreach(_pkg GIF PNG OpenEXR benchmark GTest)
		set(CMAKE_DISABLE_FIND_PACKAGE_${_pkg} TRUE)
	endforeach()
	list(PREPEND CMAKE_MODULE_PATH "${IMGCAT2_VENDORED_FIND_DIR}")
	imgcat2_vendor_begin(NO_INSTALL)
	add_subdirectory(vendor/libjxl EXCLUDE_FROM_ALL)
	imgcat2_vendor_end()
	list(REMOVE_ITEM CMAKE_MODULE_PATH "${IMGCAT2_VENDORED_FIND_DIR}")
	foreach(_pkg GIF PNG OpenEXR benchmark GTest)
		unset(CMAKE_DISABLE_FIND_PACKAGE_${_pkg})
	endforeach()

	set(JXL_FOUND TRUE)
	set(JXL_LIBRARIES jxl jxl_cms)
	set(JXL_CUSTOM_LIBS ${JXL_LIBRARIES})
	set(JXL_INCLUDE_DIRS "${PROJECT_BINARY_DIR}/vendor/libjxl/lib/include")
	set(JXL_VERSION "0.13.0")
	set(JXL_LIB_TYPE "vendored libjxl ${JXL_VERSION}; highway ${IMGCAT2_VENDORED_HWY_VERSION}, ${BROTLI_LIB_TYPE}, lcms2: ${LCMS2_LIB_TYPE}")
	img2_feature(JXL LIBS ${JXL_LIBRARIES} INCLUDE_DIRS ${JXL_INCLUDE_DIRS})

elseif(IMG2_ENABLE_JXL)
	set(JXL_LIB_TYPE "host")
	pkg_check_modules(JXL libjxl)
	if(JXL_FOUND)
		if(UNIX AND NOT APPLE)
			# Host -lhwy/-lbrotli*/-llcms2 next to vendored copies: see imgcat2_strip_vendored_ldflags
			imgcat2_strip_vendored_ldflags(JXL_STATIC_LDFLAGS)
		endif()
		# On macOS, explicitly find static libraries
		if(APPLE)
			find_library(JXL_STATIC_LIB NAMES libjxl.a)
			if(NOT JXL_STATIC_LIB)
				# Fall back to dynamic library if static not available
				set(_OLD_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
				set(CMAKE_FIND_LIBRARY_SUFFIXES ".dylib")
				find_library(JXL_STATIC_LIB NAMES jxl REQUIRED)
				set(CMAKE_FIND_LIBRARY_SUFFIXES ${_OLD_SUFFIXES})
				message(STATUS "  libjxl: using dynamic library ${JXL_STATIC_LIB}")
			endif()
			find_library(JXL_CMS_STATIC NAMES libjxl_cms.a)
			if(NOT JXL_CMS_STATIC)
				set(_OLD_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
				set(CMAKE_FIND_LIBRARY_SUFFIXES ".dylib")
				find_library(JXL_CMS_STATIC NAMES jxl_cms)
				set(CMAKE_FIND_LIBRARY_SUFFIXES ${_OLD_SUFFIXES})
			endif()
			set(JXL_CUSTOM_LIBS ${JXL_STATIC_LIB})
			if(JXL_CMS_STATIC)
				list(APPEND JXL_CUSTOM_LIBS ${JXL_CMS_STATIC})
			endif()

			# Add JXL dependencies (fall back to dynamic if static not available)
			find_library(HWY_STATIC NAMES libhwy.a)
			if(HWY_STATIC)
				list(APPEND JXL_CUSTOM_LIBS ${HWY_STATIC})
			else()
				# libhwy static not available, use dynamic library
				# Temporarily allow .dylib suffix for this search
				set(_OLD_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
				set(CMAKE_FIND_LIBRARY_SUFFIXES ".dylib")
				find_library(HWY_DYNAMIC NAMES hwy
					PATHS /usr/local/opt/highway/lib /usr/local/Cellar/highway/1.3.0/lib
					REQUIRED)
				set(CMAKE_FIND_LIBRARY_SUFFIXES ${_OLD_SUFFIXES})
				list(APPEND JXL_CUSTOM_LIBS ${HWY_DYNAMIC})
				message(STATUS "  libhwy: using dynamic library ${HWY_DYNAMIC}")
			endif()

			find_library(BROTLIENC_STATIC NAMES libbrotlienc.a)
			find_library(BROTLIDEC_STATIC NAMES libbrotlidec.a)
			find_library(BROTLICOMMON_STATIC NAMES libbrotlicommon.a)
			if(BROTLIENC_STATIC)
				list(APPEND JXL_CUSTOM_LIBS ${BROTLIENC_STATIC})
			endif()
			if(BROTLIDEC_STATIC)
				list(APPEND JXL_CUSTOM_LIBS ${BROTLIDEC_STATIC})
			endif()
			if(BROTLICOMMON_STATIC)
				list(APPEND JXL_CUSTOM_LIBS ${BROTLICOMMON_STATIC})
			endif()
		endif()

		if(APPLE)
			img2_feature(JXL LIBS ${JXL_CUSTOM_LIBS} INCLUDE_DIRS ${JXL_INCLUDE_DIRS})
		else()
			img2_feature(JXL LIBS ${JXL_STATIC_LDFLAGS} INCLUDE_DIRS ${JXL_INCLUDE_DIRS})
		endif()
		message(STATUS "libjxl found: ${JXL_VERSION}")

	else()
		message(WARNING "libjxl not found, JPEG XL support disabled")
		set(IMG2_ENABLE_JXL OFF)
	endif()
endif()
