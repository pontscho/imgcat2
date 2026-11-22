# Purpose: HEIF / AVIF (optional): vendored libheif + libde265/kvazaar/aom/openjpeg, or host.
# Inputs:  IMG2_ENABLE_HEIF, IMG2_USE_SYSTEM_HEIF, JPEG/WebP/brotli results.
# Outputs: HEIF_FOUND, HEIF_LIBRARIES, HEIF_INCLUDE_DIRS, HEIF_LIB_TYPE, HEIF_CODECS, AOM::aom, img2_feature(HEIF).

# HEIF / AVIF (optional)
if(IMG2_ENABLE_HEIF AND NOT IMG2_USE_SYSTEM_HEIF)
	# Vendored HEIF stack, everything static and built into libheif (no plugin loading):
	#   libheif 1.23.7          container + codec glue                    LGPL-3.0
	#   libde265 1.1.3          HEVC decoder                              LGPL-3.0
	#   kvazaar 2.3.2           HEVC encoder (x265 is GPL: not used)      BSD-3-Clause
	#   aom 3.15.3              AV1 decoder + encoder (AVIF)              BSD-2-Clause + AOM patent license
	#   OpenJPEG 2.5.4          JPEG 2000 decoder + encoder (j2k in HEIF) BSD-2-Clause
	#   libjpeg-turbo, zlib-ng, brotli, libsharpyuv: the shared vendored copies
	# License: libheif and libde265 are LGPL-3.0. Static linking is allowed, but whoever
	# distributes the imgcat2 binary must let recipients relink it against a modified
	# libheif/libde265 (LGPL-3.0 section 4(d)), e.g. by offering imgcat2's source or
	# object files. dav1d/rav1e/SVT-AV1/ffmpeg/x264/openh264/vvenc/vvdec/uvg266: not used.
	enable_language(CXX)
	find_package(Threads REQUIRED)
	set(IMGCAT2_VENDORED_FIND_DIR "${PROJECT_SOURCE_DIR}/cmake/vendored-find")

	# --- libde265 (HEVC decoding): upstream CMake, library only. Vendor knobs here and below are normal
	# variables (vendor section: CMAKE_POLICY_DEFAULT_CMP0077 NEW), never cache entries.
	set(ENABLE_SDL OFF)                          # no SDL
	set(ENABLE_DECODER OFF)                      # no dec265 tool
	set(ENABLE_ENCODER OFF)                      # no experimental encoder
	set(ENABLE_SHERLOCK265 OFF)                  # no sherlock265
	set(ENABLE_INTERNAL_DEVELOPMENT_TOOLS OFF)   # no dev tools
	imgcat2_vendor_begin(NO_INSTALL)
	add_subdirectory(vendor/libde265 EXCLUDE_FROM_ALL)
	imgcat2_vendor_end()
	# Public headers staged into their own include dir: the libde265 source/binary roots
	# also hold config.h and other generic names that must not leak into libheif
	set(LIBDE265_STAGE_DIR "${PROJECT_BINARY_DIR}/_deps/libde265-include")
	configure_file("${PROJECT_SOURCE_DIR}/vendor/libde265/libde265/de265.h"
		"${LIBDE265_STAGE_DIR}/libde265/de265.h" COPYONLY)
	configure_file("${PROJECT_BINARY_DIR}/vendor/libde265/libde265/de265-version.h"
		"${LIBDE265_STAGE_DIR}/libde265/de265-version.h" COPYONLY)
	set(IMGCAT2_VENDORED_LIBDE265_LIBRARIES de265)
	set(IMGCAT2_VENDORED_LIBDE265_INCLUDE_DIRS "${LIBDE265_STAGE_DIR}")
	set(IMGCAT2_VENDORED_LIBDE265_VERSION "1.1.3")

	# --- kvazaar (HEVC encoding): pattern B, hand-written static target. Upstream's CMake
	# runs "git submodule update" in its source dir, writes version.h/kvazaar.pc.temp into
	# src/ and registers shell tests unconditionally, so it is not add_subdirectory()'d.
	# Source list mirrors upstream: src/*.c minus the CLI, src/strategies/**/*.c, libmd5.
	set(KVAZAAR_SRC_DIR "${PROJECT_SOURCE_DIR}/vendor/kvazaar")
	set(KVAZAAR_GEN_DIR "${PROJECT_BINARY_DIR}/_deps/kvazaar-gen")
	file(GLOB KVAZAAR_SOURCES "${KVAZAAR_SRC_DIR}/src/*.c")
	list(REMOVE_ITEM KVAZAAR_SOURCES
		"${KVAZAAR_SRC_DIR}/src/encmain.c"
		"${KVAZAAR_SRC_DIR}/src/cli.c"
		"${KVAZAAR_SRC_DIR}/src/yuv_io.c")
	file(GLOB_RECURSE KVAZAAR_STRATEGY_SOURCES "${KVAZAAR_SRC_DIR}/src/strategies/*.c")
	file(STRINGS "${KVAZAAR_SRC_DIR}/CMakeLists.txt" _kvz_version_line REGEX "^VERSION [0-9]+\\.[0-9]+\\.[0-9]+")
	string(REGEX REPLACE "^VERSION ([0-9.]+).*$" "\\1" KVAZAAR_VERSION "${_kvz_version_line}")
	function(imgcat2_kvazaar_version_header _in _out)
		# Variables of upstream's version.h.in, scoped to this function
		set(PROJECT_VERSION "${KVAZAAR_VERSION}")
		set(KVZ_COMPILER_STRING "${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
		string(TIMESTAMP CMAKE_BUILD_DATE %Y-%m-%d)
		configure_file("${_in}" "${_out}" @ONLY)
	endfunction()
	imgcat2_kvazaar_version_header("${KVAZAAR_SRC_DIR}/src/version.h.in" "${KVAZAAR_GEN_DIR}/private/version.h")
	configure_file("${KVAZAAR_SRC_DIR}/src/kvazaar.h" "${KVAZAAR_GEN_DIR}/include/kvazaar.h" COPYONLY)
	add_library(vendor_kvazaar STATIC
		${KVAZAAR_SOURCES}
		${KVAZAAR_STRATEGY_SOURCES}
		"${KVAZAAR_SRC_DIR}/src/extras/libmd5.c")
	target_include_directories(vendor_kvazaar
		PUBLIC "${KVAZAAR_GEN_DIR}/include"
		PRIVATE "${KVAZAAR_GEN_DIR}/private" "${KVAZAAR_SRC_DIR}/src"
			"${KVAZAAR_SRC_DIR}/src/extras" "${KVAZAAR_SRC_DIR}/src/strategies")
	# KVZ_DLL_EXPORTS without PIC = "building the static library"; KVZ_STATIC_LIB for users
	target_compile_definitions(vendor_kvazaar
		PRIVATE CMAKE_BUILD KVZ_DLL_EXPORTS
		INTERFACE KVZ_STATIC_LIB)
	if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
		# Runtime-dispatched SIMD kernels, flags as upstream
		file(GLOB _kvz_avx2_sources "${KVAZAAR_SRC_DIR}/src/strategies/avx2/*.c")
		file(GLOB _kvz_sse41_sources "${KVAZAAR_SRC_DIR}/src/strategies/sse41/*.c")
		set_property(SOURCE ${_kvz_avx2_sources} APPEND PROPERTY COMPILE_OPTIONS
			-mavx2 -mbmi -mpopcnt -mlzcnt -mbmi2)
		set_property(SOURCE ${_kvz_sse41_sources} APPEND PROPERTY COMPILE_OPTIONS -msse4.1)
	endif()
	target_link_libraries(vendor_kvazaar PUBLIC Threads::Threads)
	if(UNIX)
		target_link_libraries(vendor_kvazaar PUBLIC m)
	endif()
	set_target_properties(vendor_kvazaar PROPERTIES
		POSITION_INDEPENDENT_CODE ON
		C_CLANG_TIDY "")
	set(IMGCAT2_VENDORED_KVAZAAR_LIBRARIES vendor_kvazaar)
	set(IMGCAT2_VENDORED_KVAZAAR_INCLUDE_DIRS "${KVAZAAR_GEN_DIR}/include")

	# --- aom (AV1 decoding + encoding): ExternalProject, like libjpeg-turbo. aom puts
	# several hundred HAVE_*/CONFIG_* entries into the CMake cache (HAVE_AVX2, HAVE_SSE4_1,
	# ...), which would collide with other subprojects' variables under add_subdirectory().
	# Library only; runtime CPU detection; nasm for the x86 assembly; no libyuv/libwebm
	# (only used by the aomenc/aomdec apps), no VMAF/butteraugli tuning, no in-tree highway.
	include(ExternalProject)
	set(AOM_PREFIX "${PROJECT_BINARY_DIR}/_deps/aom-install")
	set(AOM_LIBRARY "${AOM_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}aom${CMAKE_STATIC_LIBRARY_SUFFIX}")
	set(_aom_cache_args
		-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}
		-DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}
		-DCMAKE_BUILD_TYPE:STRING=Release
		-DCMAKE_INSTALL_PREFIX:PATH=${AOM_PREFIX}
		-DCMAKE_INSTALL_LIBDIR:PATH=lib
		-DCMAKE_INSTALL_INCLUDEDIR:PATH=include
		-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=${IMG2_POSITION_INDEPENDENT_CODE}
		-DBUILD_SHARED_LIBS:BOOL=OFF
		-DENABLE_APPS:BOOL=OFF
		-DENABLE_DOCS:BOOL=OFF
		-DENABLE_EXAMPLES:BOOL=OFF
		-DENABLE_TESTS:BOOL=OFF
		-DENABLE_TESTDATA:BOOL=OFF
		-DENABLE_TOOLS:BOOL=OFF
		-DENABLE_NASM:BOOL=ON
		-DCONFIG_AV1_DECODER:STRING=1
		-DCONFIG_AV1_ENCODER:STRING=1
		-DCONFIG_RUNTIME_CPU_DETECT:STRING=1
		-DCONFIG_LIBYUV:STRING=0
		-DCONFIG_WEBM_IO:STRING=0
		-DCONFIG_TUNE_VMAF:STRING=0
		-DCONFIG_TUNE_BUTTERAUGLI:STRING=0
		-DCONFIG_HIGHWAY:STRING=0
	)
	if(IMG2_HARDENING_FLAGS_STRING)
		# [F32] hardening flags of the vendor section (C/C++ only; the NASM flags stay upstream's)
		list(APPEND _aom_cache_args "-DCMAKE_C_FLAGS:STRING=${IMG2_HARDENING_FLAGS_STRING}"
			"-DCMAKE_CXX_FLAGS:STRING=${IMG2_HARDENING_FLAGS_STRING}")
	endif()
	if(APPLE)
		list(APPEND _aom_cache_args
			"-DCMAKE_OSX_ARCHITECTURES:STRING=${CMAKE_OSX_ARCHITECTURES}"
			"-DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=${CMAKE_OSX_DEPLOYMENT_TARGET}"
			"-DCMAKE_OSX_SYSROOT:PATH=${CMAKE_OSX_SYSROOT}")
	endif()
	ExternalProject_Add(aom-ext
		SOURCE_DIR "${PROJECT_SOURCE_DIR}/vendor/aom"
		BINARY_DIR "${PROJECT_BINARY_DIR}/_deps/aom-build"
		INSTALL_DIR "${AOM_PREFIX}"
		CMAKE_CACHE_ARGS ${_aom_cache_args}
		BUILD_BYPRODUCTS "${AOM_LIBRARY}"
		UPDATE_COMMAND ""
		EXCLUDE_FROM_ALL TRUE
	)
	# Headers are installed at build time; the directory must exist at generate time
	file(MAKE_DIRECTORY "${AOM_PREFIX}/include")
	if(NOT TARGET AOM::aom)
		add_library(AOM::aom STATIC IMPORTED GLOBAL)
		set_target_properties(AOM::aom PROPERTIES
			IMPORTED_LOCATION "${AOM_LIBRARY}"
			INTERFACE_INCLUDE_DIRECTORIES "${AOM_PREFIX}/include"
			INTERFACE_LINK_LIBRARIES "Threads::Threads;$<$<PLATFORM_ID:Linux>:m>")
		add_dependencies(AOM::aom aom-ext)
	endif()
	set(IMGCAT2_VENDORED_AOM_LIBRARIES AOM::aom)
	set(IMGCAT2_VENDORED_AOM_INCLUDE_DIRS "${AOM_PREFIX}/include")

	# --- OpenJPEG (JPEG 2000 in HEIF, as the host libheif had): upstream CMake, openjp2 only
	set(BUILD_CODEC OFF)            # no opj_* tools
	set(BUILD_STATIC_LIBS ON)       # static library
	set(BUILD_JPIP OFF)             # no JPIP
	set(BUILD_DOC OFF)              # no docs
	set(BUILD_LUTS_GENERATOR OFF)   # no LUT generator
	set(BUILD_UNIT_TESTS OFF)       # no unit tests
	imgcat2_vendor_begin(NO_INSTALL)
	add_subdirectory(vendor/openjpeg EXCLUDE_FROM_ALL)
	imgcat2_vendor_end()
	# OpenJPEG declares these as CACHE entries; they would relocate every target created
	# afterwards (imgcat2 included) into the openjpeg build dir
	unset(EXECUTABLE_OUTPUT_PATH CACHE)
	unset(LIBRARY_OUTPUT_PATH CACHE)
	set(OPENJPEG_STAGE_DIR "${PROJECT_BINARY_DIR}/_deps/openjpeg-include")
	configure_file("${PROJECT_SOURCE_DIR}/vendor/openjpeg/src/lib/openjp2/openjpeg.h"
		"${OPENJPEG_STAGE_DIR}/openjpeg.h" COPYONLY)
	configure_file("${PROJECT_BINARY_DIR}/vendor/openjpeg/src/lib/openjp2/opj_config.h"
		"${OPENJPEG_STAGE_DIR}/opj_config.h" COPYONLY)
	set(IMGCAT2_VENDORED_OPENJPEG_LIBRARIES openjp2)
	set(IMGCAT2_VENDORED_OPENJPEG_INCLUDE_DIRS "${OPENJPEG_STAGE_DIR}")

	# --- libsharpyuv (vendored libwebp) for sharp RGB->YCbCr conversion, and libjpeg
	if(TARGET sharpyuv)
		set(IMGCAT2_VENDORED_LIBSHARPYUV_LIBRARIES sharpyuv)
		set(IMGCAT2_VENDORED_LIBSHARPYUV_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/vendor/libwebp")
		set(WITH_LIBSHARPYUV ON)    # libheif: sharpyuv from vendored libwebp
	else()
		set(WITH_LIBSHARPYUV OFF)   # libheif: no sharpyuv (WebP disabled or host)
	endif()
	if(TARGET libjpeg-turbo-ext)
		set(IMGCAT2_VENDORED_JPEG_LIBRARIES JPEG::JPEG)
		set(IMGCAT2_VENDORED_JPEG_INCLUDE_DIRS "${JPEG_INCLUDE_DIRS}")
	endif()

	# --- libheif: static, codecs built in, no plugin loading / examples / tests / docs
	set(ENABLE_PLUGIN_LOADING OFF)   # no dynamic codec plugins
	foreach(_codec LIBDE265 KVAZAAR AOM_DECODER AOM_ENCODER JPEG_DECODER JPEG_ENCODER
			OpenJPEG_DECODER OpenJPEG_ENCODER)
		set(WITH_${_codec} ON)            # built-in codec
		set(WITH_${_codec}_PLUGIN OFF)    # not as plugin
	endforeach()
	foreach(_codec X265 X264 DAV1D SvtEnc RAV1E FFMPEG_DECODER OPENJPH_ENCODER UVG266
			VVDEC VVENC OpenH264_DECODER)
		set(WITH_${_codec} OFF)           # codec not used
	endforeach()
	set(WITH_UNCOMPRESSED_CODEC ON)       # ISO/IEC 23001-17 (zlib-ng + brotli)
	set(WITH_WEBCODECS OFF)               # no web codecs
	set(WITH_LIBSHARPYUV_INTERNAL OFF)    # no third-party/ libsharpyuv
	set(WITH_EXAMPLES OFF)                # no examples
	set(WITH_EXAMPLE_HEIF_THUMB OFF)      # no heif-thumbnailer
	set(WITH_EXAMPLE_HEIF_VIEW OFF)       # no heif-view
	set(WITH_GDK_PIXBUF OFF)              # no gdk-pixbuf loader
	set(WITH_FUZZERS OFF)                 # no fuzzers
	set(BUILD_DOCUMENTATION OFF)          # no doxygen docs
	set(BUILD_DEVELOPMENT_TOOLS OFF)      # no development tools
	set(ENABLE_EXPERIMENTAL_FEATURES OFF) # stable API only
	list(PREPEND CMAKE_MODULE_PATH "${IMGCAT2_VENDORED_FIND_DIR}")
	imgcat2_vendor_begin(NO_INSTALL)
	add_subdirectory(vendor/libheif EXCLUDE_FROM_ALL)
	imgcat2_vendor_end()
	list(REMOVE_ITEM CMAKE_MODULE_PATH "${IMGCAT2_VENDORED_FIND_DIR}")

	set(HEIF_FOUND TRUE)
	set(HEIF_LIBRARIES heif)
	set(HEIF_CUSTOM_LIBS heif)
	set(HEIF_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/vendor/libheif/libheif/api" "${PROJECT_BINARY_DIR}/vendor/libheif")
	set(HEIF_LIB_TYPE "vendored libheif 1.23.7")
	set(HEIF_CODECS "libde265 (HEVC dec)" "kvazaar (HEVC enc)" "aom (AV1 dec+enc)"
		"openjpeg (J2K dec+enc)" "libjpeg-turbo (JPEG dec+enc)" "uncompressed (zlib-ng+brotli)")
	if(TARGET sharpyuv)
		list(APPEND HEIF_CODECS "sharpyuv")
	endif()
	# Fat archive inputs in the former link order: libheif, libde265, aom, openjp2, kvazaar (sharpyuv, libjpeg,
	# zlib-ng and brotli are registered by their own modules / Img2LinkOrder)
	img2_feature(HEIF LIBS ${HEIF_CUSTOM_LIBS} INCLUDE_DIRS ${HEIF_INCLUDE_DIRS}
		ARCHIVES "$<TARGET_FILE:heif>" "$<TARGET_FILE:de265>" "${AOM_LIBRARY}" "$<TARGET_FILE:openjp2>"
		         "$<TARGET_FILE:vendor_kvazaar>"
		ARCHIVE_TARGETS heif de265 aom-ext openjp2 vendor_kvazaar)

elseif(IMG2_ENABLE_HEIF)
	set(HEIF_LIB_TYPE "host")
	pkg_check_modules(HEIF libheif)

	if(HEIF_FOUND)
		# Detect optional HEIF dependencies (codecs)
		# These are optional - libheif can work without them.
		# x265 (GPL) is not linked even if the host libheif was built with it: HEVC
		# encoding goes through kvazaar (a host libheif.a that references x265 will
		# fail to link; use the vendored stack, IMG2_USE_SYSTEM_HEIF=OFF, in that case).
		pkg_check_modules(DE265 libde265)
		pkg_check_modules(KVAZAAR kvazaar)
		pkg_check_modules(AOM aom)
		pkg_check_modules(OPENJP2 libopenjp2)

		find_library(BROTLIDEC_LIB NAMES brotlidec)
		find_library(BROTLIENC_LIB NAMES brotlienc)
		find_library(BROTLICOMMON_LIB NAMES brotlicommon)
		# find_library(HEIF_STATIC_LIB NAMES libheif.a REQUIRED)

		# Build custom link flags for static linking
		# Only add codecs that have static libraries available
		if(UNIX AND NOT APPLE)
			find_library(HEIF_STATIC_LIB NAMES libheif.a REQUIRED)
			# Start with base libheif (without automatic dependencies)
			set(HEIF_CUSTOM_LIBS ${HEIF_STATIC_LIB})

			# Check and add optional codecs
			if(DE265_FOUND)
				find_library(DE265_STATIC NAMES libde265.a)
				if(DE265_STATIC)
					list(APPEND HEIF_CUSTOM_LIBS ${DE265_STATIC})

				else()
					message(STATUS "  libde265 found but no static library, skipping")
					set(DE265_FOUND FALSE)
				endif()
			endif()

		if(KVAZAAR_FOUND)
			find_library(KVAZAAR_STATIC NAMES libkvazaar.a)
			if(KVAZAAR_STATIC)
				list(APPEND HEIF_CUSTOM_LIBS ${KVAZAAR_STATIC})

			else()
				message(STATUS "  libkvazaar found but no static library, skipping")
				set(KVAZAAR_FOUND FALSE)
			endif()
		endif()
			if(AOM_FOUND)
				find_library(AOM_STATIC NAMES libaom.a)
				if(AOM_STATIC)
					list(APPEND HEIF_CUSTOM_LIBS ${AOM_STATIC})

				else()
					message(STATUS "  libaom found but no static library, skipping")
					set(AOM_FOUND FALSE)
				endif()
			endif()

			if(OPENJP2_FOUND)
				find_library(OPENJP2_STATIC NAMES libopenjp2.a)
				if(OPENJP2_STATIC)
					list(APPEND HEIF_CUSTOM_LIBS ${OPENJP2_STATIC})

				else()
					message(STATUS "  libopenjp2 found but no static library, skipping")
					set(OPENJP2_FOUND FALSE)
				endif()
			endif()

			# Add system libraries needed by libheif and its codecs
			list(APPEND HEIF_CUSTOM_LIBS -lstdc++ -lm -lpthread)

			# Check for libaom even if pkg-config didn't find it
			if(NOT AOM_FOUND)
				find_library(AOM_LIB NAMES aom)
				if(AOM_LIB)
					list(APPEND HEIF_CUSTOM_LIBS ${AOM_LIB})
					set(AOM_FOUND TRUE)
					message(STATUS "libaom static library found and added (pkg-config unavailable)")
				endif()
			endif()

			# libheif may need brotli for compression (the vendored brotli, when built for
			# libjxl, is linked last via IMGCAT2_VENDORED_TRAILING_LIBS instead)
			if(TARGET brotlicommon)
				message(STATUS "libbrotli: vendored copy used for the host libheif")
			elseif(BROTLIDEC_LIB AND BROTLIENC_LIB AND BROTLICOMMON_LIB)
				list(APPEND HEIF_CUSTOM_LIBS ${BROTLIDEC_LIB} ${BROTLIENC_LIB} ${BROTLICOMMON_LIB})
				message(STATUS "libbrotli found and added")
			endif()

		elseif(APPLE)
			# macOS: Explicitly find static libraries
			find_library(HEIF_STATIC_LIB NAMES libheif.a REQUIRED)
			set(HEIF_CUSTOM_LIBS ${HEIF_STATIC_LIB})

			# Check and add optional codecs (static only)
			if(DE265_FOUND)
				find_library(DE265_STATIC NAMES libde265.a)
				if(DE265_STATIC)
					list(APPEND HEIF_CUSTOM_LIBS ${DE265_STATIC})
				endif()
			endif()

			if(AOM_FOUND)
				find_library(AOM_STATIC NAMES libaom.a)
				if(AOM_STATIC)
					list(APPEND HEIF_CUSTOM_LIBS ${AOM_STATIC})
					# AOM requires libvmaf
					find_library(VMAF_STATIC NAMES libvmaf.a)
					if(VMAF_STATIC)
						list(APPEND HEIF_CUSTOM_LIBS ${VMAF_STATIC})
					endif()
				endif()

		if(KVAZAAR_FOUND)
			find_library(KVAZAAR_STATIC NAMES libkvazaar.a)
			if(KVAZAAR_STATIC)
				list(APPEND HEIF_CUSTOM_LIBS ${KVAZAAR_STATIC})
			endif()
		endif()
			endif()

			# Check for libaom even if pkg-config didn't find it
			if(NOT AOM_FOUND)
				find_library(AOM_STATIC NAMES libaom.a)
				if(AOM_STATIC)
					list(APPEND HEIF_CUSTOM_LIBS ${AOM_STATIC})
					message(STATUS "libaom static library found and added")
					# AOM requires libvmaf
					find_library(VMAF_STATIC NAMES libvmaf.a)
					if(VMAF_STATIC)
						list(APPEND HEIF_CUSTOM_LIBS ${VMAF_STATIC})
						message(STATUS "libvmaf static library found and added")
					endif()
				endif()
			endif()

			# Add libsharpyuv (WebP dependency that AOM might need)
			find_library(SHARPYUV_STATIC NAMES libsharpyuv.a)
			if(SHARPYUV_STATIC)
				list(APPEND HEIF_CUSTOM_LIBS ${SHARPYUV_STATIC})
			endif()
		endif()

		img2_feature(HEIF LIBS ${HEIF_CUSTOM_LIBS} INCLUDE_DIRS ${HEIF_INCLUDE_DIRS} SYSTEM_LIBS ${HEIF_CUSTOM_LIBS})
		img2_warn_host_dependency("libheif (IMG2_USE_SYSTEM_HEIF)")

	else()
		message(WARNING "libheif not found, HEIF support disabled")
		set(IMG2_ENABLE_HEIF OFF)
	endif()
endif()
