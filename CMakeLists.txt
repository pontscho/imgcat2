cmake_minimum_required(VERSION 3.15)
project(imgcat2 VERSION 1.0.0 LANGUAGES C)

# Set default build type to RelWithDebInfo if not specified
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the type of build (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
endif()

# Export compile_commands.json for clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)  # GNU extensions for gnu11

# Build options
option(ENABLE_GIF "Enable GIF support (giflib)" ON)
option(ENABLE_WEBP "Enable WebP support" ON)
option(ENABLE_HEIF "Enable HEIF support" ON)
option(ENABLE_TIFF "Enable TIFF support" ON)
option(ENABLE_RAW "Enable RAW support (libraw)" ON)
option(ENABLE_JXL "Enable JPEG XL support (libjxl)" ON)
option(BUILD_TESTING "Build tests" ON)
option(ENABLE_CLANG_TIDY "Enable clang-tidy static analysis" OFF)
option(ENABLE_CLANG_FORMAT "Enable clang-format code formatting" ON)

# Static linking preference
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)

# Platform-specific static linking
# macOS doesn't support fully static binaries (libSystem must be dynamic)
# Only link third-party libraries statically on macOS
if(UNIX AND NOT APPLE)
	# Linux: Full static linking
	set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
elseif(APPLE)
	# macOS: Static link third-party libs, dynamic system libs
	# Only search for .a files to force static linking
	set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
endif()

# clang-tidy detection
find_program(CLANG_TIDY_EXE NAMES clang-tidy)
if(ENABLE_CLANG_TIDY)
	if(CLANG_TIDY_EXE)
		set(CMAKE_C_CLANG_TIDY "${CLANG_TIDY_EXE}")
		message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
	else()
		message(STATUS "clang-tidy not found, static analysis disabled")
		set(ENABLE_CLANG_TIDY OFF)
	endif()
endif()

# clang-format detection
find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(ENABLE_CLANG_FORMAT)
	if(CLANG_FORMAT_EXE)
		message(STATUS "clang-format found: ${CLANG_FORMAT_EXE}")
	else()
		message(STATUS "clang-format not found, code formatting disabled")
		set(ENABLE_CLANG_FORMAT OFF)
	endif()
endif()

# Compiler flags
add_compile_options(-Wall -Wextra -Werror -Wno-error=unused-but-set-variable -O3 -g)

# Stack size linker flags
if(UNIX AND NOT APPLE)
	# Linux
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,stack-size=8388608")
elseif(APPLE)
	# macOS
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-stack_size,0x800000")
elseif(WIN32)
	# Windows
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /STACK:8388608")
endif()

# pkg-config
find_package(PkgConfig REQUIRED)

# zlib-ng (preferred) or zlib
pkg_check_modules(ZLIB_NG zlib-ng)
if(NOT ZLIB_NG_FOUND)
	# On macOS, find static library explicitly
	if(APPLE)
		find_library(ZLIB_LIBRARY NAMES z REQUIRED
			HINTS /usr/local/opt/zlib/lib /usr/local/lib)
		find_path(ZLIB_INCLUDE_DIR zlib.h
			HINTS /usr/local/opt/zlib/include /usr/local/include)
		if(ZLIB_LIBRARY AND ZLIB_INCLUDE_DIR)
			set(ZLIB_LIBRARIES ${ZLIB_LIBRARY})
			set(ZLIB_INCLUDE_DIRS ${ZLIB_INCLUDE_DIR})
			set(ZLIB_FOUND TRUE)
		endif()
	else()
		# Try pkg-config first (better for finding static libraries)
		pkg_check_modules(ZLIB zlib)
		if(NOT ZLIB_FOUND)
			# Fallback to CMake's FindZLIB
			find_package(ZLIB REQUIRED)
			set(ZLIB_LIBRARIES ${ZLIB_LIBRARIES})
			set(ZLIB_INCLUDE_DIRS ${ZLIB_INCLUDE_DIRS})
		endif()
	endif()
else()
	set(ZLIB_LIBRARIES ${ZLIB_NG_LIBRARIES})
	set(ZLIB_INCLUDE_DIRS ${ZLIB_NG_INCLUDE_DIRS})
endif()

# libjpeg-turbo (preferred) or libjpeg (REQUIRED)
pkg_check_modules(JPEG libjpeg-turbo)
if(NOT JPEG_FOUND)
	find_package(JPEG)
endif()

if(NOT JPEG_FOUND)
	message(FATAL_ERROR "libjpeg or libjpeg-turbo is required but not found")
endif()
add_definitions(-DHAVE_LIBJPEG)

# libpng (REQUIRED)
# Find static library explicitly on macOS
if(APPLE)
	find_library(PNG_LIBRARY NAMES png16 png REQUIRED)
	find_path(PNG_INCLUDE_DIR png.h PATH_SUFFIXES libpng16 libpng)
	if(PNG_LIBRARY AND PNG_INCLUDE_DIR)
		set(PNG_LIBRARIES ${PNG_LIBRARY})
		set(PNG_INCLUDE_DIRS ${PNG_INCLUDE_DIR})
		set(PNG_FOUND TRUE)
	endif()
else()
	pkg_check_modules(PNG libpng16)
	if(NOT PNG_FOUND)
		pkg_check_modules(PNG libpng)
	endif()
endif()

if(NOT PNG_FOUND)
	message(FATAL_ERROR "libpng is required but not found")
endif()
add_definitions(-DHAVE_LIBPNG)

# Check if libpng has APNG support (requires libpng 1.6.0+)
include(CheckCSourceCompiles)
# Prefer APNG-enabled libpng if available
if(EXISTS "/usr/local/libpng-apng/include/png.h")
	set(PNG_INCLUDE_DIRS "/usr/local/libpng-apng/include")
	set(PNG_LIBRARIES "/usr/local/libpng-apng/lib/libpng16.a")
	message(STATUS "Found APNG-enabled libpng: ${PNG_LIBRARIES}")
endif()
set(CMAKE_REQUIRED_INCLUDES ${PNG_INCLUDE_DIRS})
set(CMAKE_REQUIRED_LIBRARIES ${PNG_LIBRARIES} ${ZLIB_LIBRARIES})
# Add math library for libpng (needed for pow, floor functions)
if(UNIX AND NOT APPLE)
	list(APPEND CMAKE_REQUIRED_LIBRARIES m)
endif()

check_c_source_compiles("
	#include <png.h>
	#ifndef PNG_APNG_SUPPORTED
	#error PNG_APNG_SUPPORTED not defined
	#endif
	int main(void) {
		png_uint_32 num_frames = 0;
		png_uint_32 num_plays = 0;
		png_structp png_ptr = NULL;
		png_infop info_ptr = NULL;
		png_get_acTL(png_ptr, info_ptr, &num_frames, &num_plays);
		return 0;
	}
" PNG_APNG_SUPPORTED_CHECK)

if(PNG_APNG_SUPPORTED_CHECK)
	# PNG_APNG_SUPPORTED is already defined in png.h when APNG support is available
	# Don't redefine it here to avoid macro redefinition warnings
	message(STATUS "libpng has APNG support: YES")

else()
	message(WARNING "libpng does not have APNG support, APNG animations will not be decoded")
endif()

# giflib (optional)
if(ENABLE_GIF)
	find_library(GIF_LIBRARY NAMES gif)
	find_path(GIF_INCLUDE_DIR gif_lib.h)
	if(GIF_LIBRARY AND GIF_INCLUDE_DIR)
		add_definitions(-DHAVE_GIFLIB)
		set(GIF_FOUND TRUE)
	else()
		message(WARNING "giflib not found, GIF support disabled")
		set(GIF_FOUND FALSE)
		set(ENABLE_GIF OFF)
	endif()
endif()

# WebP (optional)
if(ENABLE_WEBP)
	# Find static library explicitly on macOS
	if(APPLE)
		find_library(WEBP_LIBRARY NAMES webp)
		find_library(WEBPDEMUX_LIBRARY NAMES webpdemux)
		find_path(WEBP_INCLUDE_DIR webp/decode.h)
		if(WEBP_LIBRARY AND WEBPDEMUX_LIBRARY AND WEBP_INCLUDE_DIR)
			set(WEBP_LIBRARIES ${WEBP_LIBRARY} ${WEBPDEMUX_LIBRARY})
			set(WEBP_INCLUDE_DIRS ${WEBP_INCLUDE_DIR})
			set(WEBP_FOUND TRUE)
		endif()

	else()
		pkg_check_modules(WEBP libwebp libwebpdemux)
	endif()

	if(WEBP_FOUND)
		add_definitions(-DHAVE_WEBP)
	else()
		message(WARNING "libwebp not found, WebP support disabled")
		set(ENABLE_WEBP OFF)
	endif()
endif()

# HEIF (optional)
if(ENABLE_HEIF)
	pkg_check_modules(HEIF libheif)

	if(HEIF_FOUND)
		# Detect optional HEIF dependencies (codecs)
		# These are optional - libheif can work without them
		pkg_check_modules(DE265 libde265)
		pkg_check_modules(X265 x265)
		pkg_check_modules(AOM libaom)
		pkg_check_modules(OPENJP2 libopenjp2)

		# Build custom link flags for static linking
		# Only add codecs that have static libraries available
		if(UNIX AND NOT APPLE)
			# Start with base libheif (without automatic dependencies)
			find_library(HEIF_STATIC_LIB NAMES libheif.a REQUIRED)
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
			if(X265_FOUND)
				find_library(X265_STATIC NAMES libx265.a)
				if(X265_STATIC)
					list(APPEND HEIF_CUSTOM_LIBS ${X265_STATIC})
				else()
					message(STATUS "  libx265 found but no static library, skipping")
					set(X265_FOUND FALSE)
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

			# x265 requires libnuma
			if(X265_FOUND)
				find_library(NUMA_LIB NAMES numa)
				if(NUMA_LIB)
					list(APPEND HEIF_CUSTOM_LIBS ${NUMA_LIB})
				endif()
			endif()

			# Check for libaom even if pkg-config didn't find it
			if(NOT AOM_FOUND)
				find_library(AOM_LIB NAMES aom)
				if(AOM_LIB)
					list(APPEND HEIF_CUSTOM_LIBS ${AOM_LIB})
					set(AOM_FOUND TRUE)
					message(STATUS "  libaom static library found and added (pkg-config unavailable)")
				endif()
			endif()

			# libheif may need brotli for compression
			find_library(BROTLIDEC_LIB NAMES brotlidec)
			find_library(BROTLIENC_LIB NAMES brotlienc)
			find_library(BROTLICOMMON_LIB NAMES brotlicommon)
			if(BROTLIDEC_LIB AND BROTLIENC_LIB AND BROTLICOMMON_LIB)
				list(APPEND HEIF_CUSTOM_LIBS ${BROTLIDEC_LIB} ${BROTLIENC_LIB} ${BROTLICOMMON_LIB})
				message(STATUS "  libbrotli found and added")
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
			if(X265_FOUND)
				find_library(X265_STATIC NAMES libx265.a)
				if(X265_STATIC)
					list(APPEND HEIF_CUSTOM_LIBS ${X265_STATIC})
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
			endif()

			# Check for libaom even if pkg-config didn't find it
			if(NOT AOM_FOUND)
				find_library(AOM_STATIC NAMES libaom.a)
				if(AOM_STATIC)
					list(APPEND HEIF_CUSTOM_LIBS ${AOM_STATIC})
					message(STATUS "  libaom static library found and added")
					# AOM requires libvmaf
					find_library(VMAF_STATIC NAMES libvmaf.a)
					if(VMAF_STATIC)
						list(APPEND HEIF_CUSTOM_LIBS ${VMAF_STATIC})
						message(STATUS "  libvmaf static library found and added")
					endif()
				endif()
			endif()

			# Add libsharpyuv (WebP dependency that AOM might need)
			find_library(SHARPYUV_STATIC NAMES libsharpyuv.a)
			if(SHARPYUV_STATIC)
				list(APPEND HEIF_CUSTOM_LIBS ${SHARPYUV_STATIC})
			endif()

			# Add system libraries
			list(APPEND HEIF_CUSTOM_LIBS "-lc++")
		endif()

		add_definitions(-DHAVE_HEIF)

	else()
		message(WARNING "libheif not found, HEIF support disabled")
		set(ENABLE_HEIF OFF)
	endif()
endif()

# TIFF (optional)
if(ENABLE_TIFF)
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
		endif()
		add_definitions(-DHAVE_TIFF)
	else()
		message(WARNING "libtiff not found, TIFF support disabled")
		set(ENABLE_TIFF OFF)
	endif()
endif()

# RAW (optional)
if(ENABLE_RAW)
	pkg_check_modules(RAW libraw)

	if(RAW_FOUND)
		# libraw depends on lcms2 and other libraries
		if(UNIX AND NOT APPLE)
			# Linux: verify static libraries exist
			find_library(RAW_STATIC_LIB NAMES libraw.a libraw_r.a)
			find_library(LCMS2_STATIC_LIB NAMES liblcms2.a)
			if(NOT RAW_STATIC_LIB OR NOT LCMS2_STATIC_LIB)
				message(WARNING "libraw or liblcms2 static libraries not found (required for static linking on Linux), RAW support disabled")
				set(RAW_FOUND FALSE)
			endif()
		elseif(APPLE)
			# macOS: explicitly find static libraries
			find_library(RAW_STATIC_LIB NAMES libraw.a libraw_r.a REQUIRED)
			find_library(LCMS2_STATIC NAMES liblcms2.a REQUIRED)
			set(RAW_CUSTOM_LIBS ${RAW_STATIC_LIB} ${LCMS2_STATIC})

			# Add optional dependencies
			find_library(JASPER_STATIC NAMES libjasper.a)
			if(JASPER_STATIC)
				list(APPEND RAW_CUSTOM_LIBS ${JASPER_STATIC})
			endif()

			# Add system libraries
			list(APPEND RAW_CUSTOM_LIBS "-lstdc++")
		endif()
	endif()

	if(RAW_FOUND)
		add_definitions(-DHAVE_RAW)

	else()
		message(WARNING "libraw not found, RAW support disabled")
		set(ENABLE_RAW OFF)
	endif()
endif()

# libjxl (optional)
if(ENABLE_JXL)
	pkg_check_modules(JXL libjxl)
	if(JXL_FOUND)
		# On macOS, explicitly find static libraries
		if(APPLE)
			find_library(JXL_STATIC_LIB NAMES libjxl.a REQUIRED)
			find_library(JXL_CMS_STATIC NAMES libjxl_cms.a)
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

			# Add system libraries
			list(APPEND JXL_CUSTOM_LIBS "-lc++")
		endif()

		add_definitions(-DHAVE_JXL)
		message(STATUS "libjxl found: ${JXL_VERSION}")

	else()
		message(WARNING "libjxl not found, JPEG XL support disabled")
		set(ENABLE_JXL OFF)
	endif()
endif()

# NanoSVG (optional, header-only library in vendor/)
# Check if nanosvg submodule exists
set(NANOSVG_HEADER ${CMAKE_SOURCE_DIR}/vendor/nanosvg/src/nanosvg.h)
if(EXISTS ${NANOSVG_HEADER})
else()
	message(FATAL "nanosvg not found in vendor/nanosvg/src/, SVG support disabled")
	message(FATAL "  Run: git submodule update --init --recursive")
endif()

# STB headers (vendored)
set(STB_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/vendor/stb)

# QOI headers (vendored, header-only)
set(QOI_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/vendor/qoi)

# NanoSVG headers (vendored, header-only)
set(NANOSVG_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/vendor/nanosvg/src)
include_directories(${NANOSVG_INCLUDE_DIR})

# Add include directories
include_directories(
	${CMAKE_SOURCE_DIR}/src
	${NANOSVG_INCLUDE_DIR}
	${STB_INCLUDE_DIR}
	${QOI_INCLUDE_DIR}
	${PNG_INCLUDE_DIRS}
	${ZLIB_INCLUDE_DIRS}
	${JPEG_INCLUDE_DIRS}
)

if(GIF_FOUND)
	include_directories(${GIF_INCLUDE_DIR})
endif()

if(WEBP_FOUND)
	include_directories(${WEBP_INCLUDE_DIRS})
endif()

if(HEIF_FOUND)
	include_directories(${HEIF_INCLUDE_DIRS})
endif()

if(TIFF_FOUND)
	include_directories(${TIFF_INCLUDE_DIRS})
endif()

if(RAW_FOUND)
	include_directories(${RAW_INCLUDE_DIRS})
endif()

if(JXL_FOUND)
	include_directories(${JXL_INCLUDE_DIRS})
endif()

set(IMGCAT2_SOURCES
	# Core module
	src/imgcat2/core/image.c
	src/imgcat2/core/pipeline.c
	src/imgcat2/core/cli.c
	src/imgcat2/core/base64.c
	src/imgcat2/core/metadata.c

	# Decoders module
	src/imgcat2/decoders/decoder.c
	src/imgcat2/decoders/magic.c
	src/imgcat2/decoders/decoder_stb.c
	src/imgcat2/decoders/decoder_png.c
	src/imgcat2/decoders/decoder_jpeg.c
	src/imgcat2/decoders/decoder_qoi.c
	src/imgcat2/decoders/decoder_ico.c
	src/imgcat2/decoders/decoder_svg.c

	# ANSI module
	src/imgcat2/ansi/ansi.c
	src/imgcat2/ansi/escape.c
)

if(GIF_FOUND)
	list(APPEND IMGCAT2_SOURCES src/imgcat2/decoders/decoder_gif.c)
endif()

if(WEBP_FOUND)
	list(APPEND IMGCAT2_SOURCES src/imgcat2/decoders/decoder_webp.c)
endif()

if(HEIF_FOUND)
	list(APPEND IMGCAT2_SOURCES src/imgcat2/decoders/decoder_heif.c)
	list(APPEND IMGCAT2_SOURCES src/imgcat2/decoders/decoder_avif.c)
endif()

if(TIFF_FOUND)
	list(APPEND IMGCAT2_SOURCES src/imgcat2/decoders/decoder_tiff.c)
endif()

if(RAW_FOUND)
	list(APPEND IMGCAT2_SOURCES src/imgcat2/decoders/decoder_raw.c)
endif()

if(JXL_FOUND)
	list(APPEND IMGCAT2_SOURCES src/imgcat2/decoders/decoder_jxl.c)
endif()

# Platform-specific terminal module (PHASE 8)
if(WIN32)
	list(APPEND IMGCAT2_SOURCES src/imgcat2/terminal/terminal_windows.c)

else()
	list(APPEND IMGCAT2_SOURCES src/imgcat2/terminal/terminal_unix.c)

	# iTerm2 inline images protocol module
	list(APPEND IMGCAT2_SOURCES src/imgcat2/terminal/iterm2.c)

	# Kitty graphics protocol module
	list(APPEND IMGCAT2_SOURCES src/imgcat2/terminal/kitty.c)
endif()

# Main executable
add_executable(imgcat2 ${IMGCAT2_SOURCES} src/imgcat2/main.c)

# Library for tests (without main.c)
add_library(imgcat2_lib STATIC ${IMGCAT2_SOURCES})

# Link libraries for executable
target_link_libraries(imgcat2 PRIVATE
	${PNG_LIBRARIES}
	${ZLIB_LIBRARIES}
	${JPEG_LIBRARIES}
)

# Link libraries for library
target_link_libraries(imgcat2_lib PUBLIC
	${PNG_LIBRARIES}
	${ZLIB_LIBRARIES}
	${JPEG_LIBRARIES}
)

if(GIF_FOUND)
	target_link_libraries(imgcat2 PUBLIC ${GIF_LIBRARY})
	target_link_libraries(imgcat2_lib PUBLIC ${GIF_LIBRARY})
endif()

if(WEBP_FOUND)
	target_link_libraries(imgcat2 PRIVATE ${WEBP_LIBRARIES})
	target_link_libraries(imgcat2_lib PUBLIC ${WEBP_LIBRARIES})
endif()

if(HEIF_FOUND)
	# Use custom libs for static linking on both Linux and macOS
	target_link_libraries(imgcat2 PRIVATE ${HEIF_CUSTOM_LIBS})
	target_link_libraries(imgcat2_lib PUBLIC ${HEIF_CUSTOM_LIBS})
endif()

if(TIFF_FOUND)
	if(APPLE)
		# Use custom libs on macOS for static linking
		target_link_libraries(imgcat2 PRIVATE ${TIFF_CUSTOM_LIBS})
		target_link_libraries(imgcat2_lib PUBLIC ${TIFF_CUSTOM_LIBS})
	else()
		# Use STATIC_LDFLAGS on Linux for full static linking with all dependencies
		target_link_libraries(imgcat2 PRIVATE ${TIFF_STATIC_LDFLAGS})
		target_link_libraries(imgcat2_lib PUBLIC ${TIFF_STATIC_LDFLAGS})
	endif()
endif()

if(RAW_FOUND)
	if(APPLE)
		# Use custom libs on macOS for static linking
		target_link_libraries(imgcat2 PRIVATE ${RAW_CUSTOM_LIBS})
		target_link_libraries(imgcat2_lib PUBLIC ${RAW_CUSTOM_LIBS})
	else()
		# Use STATIC_LDFLAGS on Linux for full static linking with all dependencies
		target_link_libraries(imgcat2 PRIVATE ${RAW_STATIC_LDFLAGS})
		target_link_libraries(imgcat2_lib PUBLIC ${RAW_STATIC_LDFLAGS})
	endif()
endif()

if(JXL_FOUND)
	if(APPLE)
		# Use custom libs on macOS for static linking
		target_link_libraries(imgcat2 PRIVATE ${JXL_CUSTOM_LIBS})
		target_link_libraries(imgcat2_lib PUBLIC ${JXL_CUSTOM_LIBS})
	else()
		# Use STATIC_LDFLAGS on Linux for static linking
		target_link_libraries(imgcat2 PRIVATE ${JXL_STATIC_LDFLAGS})
		target_link_libraries(imgcat2_lib PUBLIC ${JXL_STATIC_LDFLAGS})
	endif()
endif()

# Platform-specific libraries
if(UNIX AND NOT APPLE)
	target_link_libraries(imgcat2 PUBLIC m)  # Math library
	target_link_libraries(imgcat2_lib PUBLIC m)  # Math library
endif()

# Install target
install(TARGETS imgcat2 DESTINATION bin)

# clang-format target
if(ENABLE_CLANG_FORMAT AND CLANG_FORMAT_EXE)
	file(GLOB_RECURSE ALL_SOURCE_FILES
		${CMAKE_SOURCE_DIR}/src/*.c
		${CMAKE_SOURCE_DIR}/src/*.h
	)
	add_custom_target(
		format
		COMMAND ${CLANG_FORMAT_EXE}
		-i
		-style=file
		${ALL_SOURCE_FILES}
		COMMENT "Running clang-format on all source files"
	)
endif()

# Testing
if(BUILD_TESTING)
	enable_testing()
	add_subdirectory(src/tests)
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "imgcat2 Configuration Summary")
message(STATUS "============================")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "C standard: C${CMAKE_C_STANDARD}")
message(STATUS "")
message(STATUS "Core Dependencies (required):")
message(STATUS "  libpng: ${PNG_LIBRARIES}")
if(PNG_APNG_SUPPORTED_CHECK)
	message(STATUS "    APNG support: YES")
else()
	message(STATUS "    APNG support: NO (requires libpng >= 1.6.0 with --enable-apng)")
endif()
message(STATUS "  zlib: ${ZLIB_LIBRARIES}")
message(STATUS "  libjpeg: ${JPEG_LIBRARIES}")
message(STATUS "")
message(STATUS "Optional Dependencies:")
if(GIF_FOUND)
	message(STATUS "  giflib: ${GIF_LIBRARY}")
endif()
if(WEBP_FOUND)
	message(STATUS "  libwebp: ${WEBP_LIBRARIES}")
endif()
if(HEIF_FOUND)
	if(UNIX AND NOT APPLE)
		message(STATUS "  libheif: ${HEIF_CUSTOM_LIBS}")
		# Show detected codecs
		set(HEIF_CODECS "")
		if(DE265_FOUND)
			list(APPEND HEIF_CODECS "de265")
		endif()
		if(X265_FOUND)
			list(APPEND HEIF_CODECS "x265")
		endif()
		if(AOM_FOUND)
			list(APPEND HEIF_CODECS "aom")
		endif()
		if(OPENJP2_FOUND)
			list(APPEND HEIF_CODECS "openjp2")
		endif()
		if(HEIF_CODECS)
			string(REPLACE ";" ", " HEIF_CODECS_STR "${HEIF_CODECS}")
			message(STATUS "    codecs: ${HEIF_CODECS_STR}")
		else()
			message(STATUS "    codecs: (none available)")
		endif()
	else()
		message(STATUS "  libheif: ${HEIF_LDFLAGS}")
	endif()
endif()
if(TIFF_FOUND)
	if(UNIX AND NOT APPLE)
		message(STATUS "  libtiff: ${TIFF_STATIC_LDFLAGS}")
	else()
		message(STATUS "  libtiff: ${TIFF_LIBRARIES}")
	endif()
endif()
if(RAW_FOUND)
	if(UNIX AND NOT APPLE)
		message(STATUS "  libraw: ${RAW_STATIC_LDFLAGS}")
	else()
		message(STATUS "  libraw: ${RAW_LDFLAGS}")
	endif()
endif()
if(NOT GIF_FOUND AND NOT WEBP_FOUND AND NOT HEIF_FOUND AND NOT TIFF_FOUND AND NOT RAW_FOUND)
	message(STATUS "  (none)")
endif()
message(STATUS "")
message(STATUS "Optional Format Support:")
message(STATUS "  GIF:  ${ENABLE_GIF}")
message(STATUS "  WebP: ${ENABLE_WEBP}")
message(STATUS "  HEIF: ${ENABLE_HEIF}")
message(STATUS "  TIFF: ${ENABLE_TIFF}")
message(STATUS "  RAW:  ${ENABLE_RAW}")
message(STATUS "  JXL:  ${ENABLE_JXL}")
message(STATUS "")
message(STATUS "Other Options:")
message(STATUS "  Testing: ${BUILD_TESTING}")
message(STATUS "  clang-tidy: ${ENABLE_CLANG_TIDY}")
message(STATUS "  clang-format: ${ENABLE_CLANG_FORMAT}")
message(STATUS "")
