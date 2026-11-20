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
option(ENABLE_RESVG "Enable resvg SVG renderer (preferred over nanosvg)" OFF)
option(ENABLE_EXIF_READER "Enable EXIF/XMP metadata parsing (built-in)" ON)
option(BUILD_TESTING "Build tests" ON)
option(ENABLE_JPEGQS "Enable JPEG quantsmooth (enhances JPEG decoding quality)" ON)
option(ENABLE_CLANG_TIDY "Enable clang-tidy static analysis" OFF)
option(ENABLE_CLANG_FORMAT "Enable clang-format code formatting" ON)
option(ENABLE_STATIC_LIBCXX "Enable static linking of libc++ on macOS" OFF)

# Dependency source selection: OFF (default) = build the vendored copy from vendor/,
# ON = use the host library found via pkg-config / find_library (legacy behaviour)
option(USE_SYSTEM_JPEG "Use host libjpeg(-turbo) instead of vendored libjpeg-turbo" OFF)
option(USE_SYSTEM_PNG "Use host libpng instead of vendored libpng (+APNG patch)" OFF)
option(USE_SYSTEM_GIF "Use host giflib instead of vendored giflib" OFF)
option(USE_SYSTEM_WEBP "Use host libwebp instead of vendored libwebp" OFF)
option(USE_SYSTEM_TIFF "Use host libtiff instead of vendored libtiff" OFF)
option(USE_SYSTEM_LCMS2 "Use host lcms2 instead of vendored Little-CMS" OFF)
option(USE_SYSTEM_RAW "Use host libraw instead of vendored LibRaw" OFF)

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
	# macOS: Static link third-party libs
	# Only search for .a files to force static linking
	set(CMAKE_FIND_LIBRARY_SUFFIXES .a)

	# Optionally find and link static libc++ from LLVM (Homebrew)
	if(ENABLE_STATIC_LIBCXX)
		find_library(LIBCXX_STATIC NAMES c++ libc++.a
			PATHS /usr/local/opt/llvm/lib/c++ /usr/local/Cellar/llvm/*/lib/c++
			NO_DEFAULT_PATH)
		find_library(LIBCXXABI_STATIC NAMES c++abi libc++abi.a
			PATHS /usr/local/opt/llvm/lib/c++ /usr/local/Cellar/llvm/*/lib/c++
			NO_DEFAULT_PATH)

		if(LIBCXX_STATIC AND LIBCXXABI_STATIC)
			# Statically link libc++ and libc++abi
			set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -nostdlib++")
			set(STATIC_LIBCXX_LIBS ${LIBCXX_STATIC} ${LIBCXXABI_STATIC})
			message(STATUS "Using static libc++: ${LIBCXX_STATIC}")
			message(STATUS "Using static libc++abi: ${LIBCXXABI_STATIC}")

		else()
			message(WARNING "Static libc++ requested but not found, using dynamic linking")
			# Fall back to dynamic libc++
			set(DYNAMIC_LIBCXX_LIBS c++)
		endif()

	else()
		# Use dynamic libc++ (default)
		set(DYNAMIC_LIBCXX_LIBS c++)
	endif()
endif()

# clang-tidy detection
find_program(CLANG_TIDY_EXE NAMES clang-tidy)
if(ENABLE_CLANG_TIDY)
	if(CLANG_TIDY_EXE)
		# Applied as CMAKE_C_CLANG_TIDY only after the vendored dependencies are
		# declared (see "Project-only compile settings"), so vendor code is not analyzed
		set(IMGCAT2_CLANG_TIDY "${CLANG_TIDY_EXE}")
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
# Optimization flags apply to everything (vendored dependencies included).
# Warning flags (-Werror) are project-only: see IMGCAT2_WARNING_FLAGS, applied after
# the vendored dependencies are declared (COMPILE_OPTIONS is captured at target creation).
add_compile_options(-O3 -g)
set(IMGCAT2_WARNING_FLAGS -Wall -Wextra -Werror -Wno-error=unused-but-set-variable)

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

# Compression library: vendored zlib-ng (zlib-compat mode)
set(ZLIB_COMPAT ON CACHE BOOL "zlib-ng: zlib-compatible API")
set(ZLIB_ENABLE_TESTS OFF CACHE BOOL "zlib-ng: disable tests")
set(ZLIBNG_ENABLE_TESTS OFF CACHE BOOL "zlib-ng: disable tests")
set(WITH_GTEST OFF CACHE BOOL "zlib-ng: disable gtest")
set(WITH_BENCHMARKS OFF CACHE BOOL "zlib-ng: disable benchmarks")
set(WITH_OPTIM ON CACHE BOOL "zlib-ng: enable optimizations")
set(ZLIB_ALIASES ON CACHE BOOL "zlib-ng: provide zlib compatible CMake targets")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "zlib-ng: static build")
add_subdirectory(vendor/zlib-ng EXCLUDE_FROM_ALL)

# Single point of truth for zlib: every consumer (imgcat2, vendored libpng/libtiff, ...)
# links ZLIB::ZLIB / ${ZLIB_LIBRARIES}. To swap the zlib implementation, change only this block.
# zlib-ng's ZLIB_ALIASES only provides 'zlib'/'zlibstatic', so ZLIB::ZLIB is aliased here.
if(NOT TARGET ZLIB::ZLIB)
	add_library(ZLIB::ZLIB ALIAS zlib-ng)
endif()
set(ZLIB_LIBRARIES ZLIB::ZLIB)
set(ZLIB_INCLUDE_DIRS "${CMAKE_BINARY_DIR}/vendor/zlib-ng" "${CMAKE_SOURCE_DIR}/vendor/zlib-ng")
# Pre-seed FindZLIB so find_package(ZLIB) inside vendored subprojects resolves to zlib-ng
# (zlib.h/zconf.h are generated into the zlib-ng binary dir) instead of the host libz
set(ZLIB_INCLUDE_DIR "${CMAKE_BINARY_DIR}/vendor/zlib-ng" CACHE PATH "zlib include dir (vendored zlib-ng)" FORCE)
set(ZLIB_LIBRARY ZLIB::ZLIB CACHE STRING "zlib library (vendored zlib-ng target)" FORCE)
set(COMPRESSION_LIB_TYPE "zlib-ng (vendored)")

# Helper for vendored add_subdirectory() projects: their own tests/tools must never
# register with our CTest or build by default
macro(imgcat2_vendor_begin)
	set(_IMGCAT2_SAVED_BUILD_TESTING ${BUILD_TESTING})
	set(BUILD_TESTING OFF)
endmacro()
macro(imgcat2_vendor_end)
	set(BUILD_TESTING ${_IMGCAT2_SAVED_BUILD_TESTING})
endmacro()

# Host static link flags (pkg-config --static) of libraries that still come from the
# host may name dependencies that are vendored here (-ljpeg, -lwebp, -lz, ...). Linking
# the host copies next to the vendored ones gives duplicate symbols / mixed ABIs, so
# they are dropped; the vendored providers are linked last instead
# (IMGCAT2_VENDORED_TRAILING_LIBS, after all host libraries).
function(imgcat2_strip_vendored_ldflags _var)
	set(_out "")
	foreach(_f IN LISTS ${_var})
		if((TARGET ZLIB::ZLIB AND _f STREQUAL "-lz")
			OR (TARGET libjpeg-turbo-ext AND _f MATCHES "^-l(jpeg|turbojpeg)$")
			OR (TARGET png_static AND _f MATCHES "^-lpng(16)?$")
			OR (TARGET vendor_giflib AND _f STREQUAL "-lgif")
			OR (TARGET webp AND _f MATCHES "^-l(webp|webpdemux|webpmux|sharpyuv)$")
			OR (TARGET vendor_lcms2 AND _f STREQUAL "-llcms2"))
			continue()
		endif()
		list(APPEND _out "${_f}")
	endforeach()
	set(${_var} "${_out}" PARENT_SCOPE)
endfunction()

# libjpeg-turbo (REQUIRED)
if(USE_SYSTEM_JPEG)
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

else()
	# Vendored libjpeg-turbo. Upstream refuses add_subdirectory() (FATAL_ERROR in its
	# CMakeLists.txt), so it is built with ExternalProject_Add and exposed as an
	# IMPORTED target. WITH_JPEG8: libjpeg v8 API/ABI (JPEG_LIB_VERSION 80), because the
	# host static libheif.a was built against the jpeg8 ABI and resolves jpeg_* from this
	# library. Only the libjpeg API is used by imgcat2 (no tj* calls) -> no turbojpeg.
	include(ExternalProject)
	set(LIBJPEG_TURBO_PREFIX "${CMAKE_BINARY_DIR}/_deps/libjpeg-turbo-install")
	set(LIBJPEG_TURBO_LIBRARY "${LIBJPEG_TURBO_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}jpeg${CMAKE_STATIC_LIBRARY_SUFFIX}")
	set(_jpeg_cache_args
		-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}
		-DCMAKE_BUILD_TYPE:STRING=Release
		-DCMAKE_INSTALL_PREFIX:PATH=${LIBJPEG_TURBO_PREFIX}
		-DCMAKE_INSTALL_LIBDIR:PATH=lib
		-DCMAKE_INSTALL_INCLUDEDIR:PATH=include
		-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON
		-DENABLE_SHARED:BOOL=OFF
		-DENABLE_STATIC:BOOL=ON
		-DWITH_JPEG8:BOOL=ON
		-DWITH_TURBOJPEG:BOOL=OFF
		-DWITH_TOOLS:BOOL=OFF
		-DWITH_TESTS:BOOL=OFF
	)
	if(APPLE)
		list(APPEND _jpeg_cache_args
			"-DCMAKE_OSX_ARCHITECTURES:STRING=${CMAKE_OSX_ARCHITECTURES}"
			"-DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=${CMAKE_OSX_DEPLOYMENT_TARGET}"
			"-DCMAKE_OSX_SYSROOT:PATH=${CMAKE_OSX_SYSROOT}")
	endif()
	ExternalProject_Add(libjpeg-turbo-ext
		SOURCE_DIR "${CMAKE_SOURCE_DIR}/vendor/libjpeg-turbo"
		BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/libjpeg-turbo-build"
		INSTALL_DIR "${LIBJPEG_TURBO_PREFIX}"
		CMAKE_CACHE_ARGS ${_jpeg_cache_args}
		BUILD_BYPRODUCTS "${LIBJPEG_TURBO_LIBRARY}"
		UPDATE_COMMAND ""
		EXCLUDE_FROM_ALL TRUE
	)
	# Headers are installed at build time; the directory must exist at generate time
	file(MAKE_DIRECTORY "${LIBJPEG_TURBO_PREFIX}/include")
	if(NOT TARGET JPEG::JPEG)
		add_library(JPEG::JPEG STATIC IMPORTED GLOBAL)
	endif()
	set_target_properties(JPEG::JPEG PROPERTIES
		IMPORTED_LOCATION "${LIBJPEG_TURBO_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${LIBJPEG_TURBO_PREFIX}/include")
	add_dependencies(JPEG::JPEG libjpeg-turbo-ext)

	set(JPEG_FOUND TRUE)
	set(JPEG_LIBRARIES JPEG::JPEG)
	set(JPEG_INCLUDE_DIRS "${LIBJPEG_TURBO_PREFIX}/include")
	# Pre-seed FindJPEG for vendored consumers (libtiff) calling find_package(JPEG)
	set(JPEG_INCLUDE_DIR "${LIBJPEG_TURBO_PREFIX}/include" CACHE PATH "libjpeg include dir (vendored libjpeg-turbo)" FORCE)
	set(JPEG_LIBRARY "${LIBJPEG_TURBO_LIBRARY}" CACHE FILEPATH "libjpeg library (vendored libjpeg-turbo)" FORCE)
	set(JPEG_LIB_TYPE "vendored libjpeg-turbo 3.2.1, jpeg8 ABI")
endif()
add_definitions(-DHAVE_LIBJPEG)

# jpeg-quantsmooth (optional, enhances JPEG decoding quality)
if(ENABLE_JPEGQS AND JPEG_FOUND)
	set(JPEGQS_SRC_DIR ${CMAKE_SOURCE_DIR}/vendor/jpeg-quantsmooth)
	set(JPEGQS_SOURCE ${JPEGQS_SRC_DIR}/libjpegqs.c)
	set(JPEGQS_COMMON_FLAGS -Wall -O2 -Wno-error)

	if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
		# x86_64: 4 SIMD variants + dispatcher

		# base (helpers included: start/finish_decompress)
		add_library(jpegqs_base OBJECT ${JPEGQS_SOURCE})
		target_compile_definitions(jpegqs_base PRIVATE SIMD_NAME=base SIMD_BASE)
		target_compile_options(jpegqs_base PRIVATE ${JPEGQS_COMMON_FLAGS})
		target_include_directories(jpegqs_base PRIVATE ${JPEGQS_SRC_DIR} ${JPEG_INCLUDE_DIRS})

		# sse2
		add_library(jpegqs_sse2 OBJECT ${JPEGQS_SOURCE})
		target_compile_definitions(jpegqs_sse2 PRIVATE SIMD_NAME=sse2 SIMD_SSE2)
		target_compile_options(jpegqs_sse2 PRIVATE ${JPEGQS_COMMON_FLAGS} -msse2)
		target_include_directories(jpegqs_sse2 PRIVATE ${JPEGQS_SRC_DIR} ${JPEG_INCLUDE_DIRS})

		# avx2
		add_library(jpegqs_avx2 OBJECT ${JPEGQS_SOURCE})
		target_compile_definitions(jpegqs_avx2 PRIVATE SIMD_NAME=avx2 SIMD_AVX2)
		target_compile_options(jpegqs_avx2 PRIVATE ${JPEGQS_COMMON_FLAGS} -mavx2 -mfma)
		target_include_directories(jpegqs_avx2 PRIVATE ${JPEGQS_SRC_DIR} ${JPEG_INCLUDE_DIRS})

		# avx512
		add_library(jpegqs_avx512 OBJECT ${JPEGQS_SOURCE})
		target_compile_definitions(jpegqs_avx512 PRIVATE SIMD_NAME=avx512 SIMD_AVX512)
		target_compile_options(jpegqs_avx512 PRIVATE ${JPEGQS_COMMON_FLAGS} -mavx512f -mavx512dq -mavx512bw -mfma)
		target_include_directories(jpegqs_avx512 PRIVATE ${JPEGQS_SRC_DIR} ${JPEG_INCLUDE_DIRS})

		# dispatcher (SIMD_SELECT, CPUID-based runtime dispatch)
		add_library(jpegqs_dispatch OBJECT ${JPEGQS_SOURCE})
		target_compile_definitions(jpegqs_dispatch PRIVATE SIMD_SELECT)
		target_compile_options(jpegqs_dispatch PRIVATE ${JPEGQS_COMMON_FLAGS})
		target_include_directories(jpegqs_dispatch PRIVATE ${JPEGQS_SRC_DIR} ${JPEG_INCLUDE_DIRS})

		add_library(jpegqs STATIC
			$<TARGET_OBJECTS:jpegqs_base>
			$<TARGET_OBJECTS:jpegqs_sse2>
			$<TARGET_OBJECTS:jpegqs_avx2>
			$<TARGET_OBJECTS:jpegqs_avx512>
			$<TARGET_OBJECTS:jpegqs_dispatch>
		)

		# Disable clang-tidy for vendor OBJECT targets
		foreach(_t jpegqs_base jpegqs_sse2 jpegqs_avx2 jpegqs_avx512 jpegqs_dispatch)
			set_target_properties(${_t} PROPERTIES C_CLANG_TIDY "")
		endforeach()

	elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
		# ARM64: NEON baseline, no runtime dispatch needed
		add_library(jpegqs STATIC ${JPEGQS_SOURCE})
		target_compile_options(jpegqs PRIVATE ${JPEGQS_COMMON_FLAGS})
		target_include_directories(jpegqs PRIVATE ${JPEGQS_SRC_DIR} ${JPEG_INCLUDE_DIRS})

	else()
		# Fallback: scalar only
		add_library(jpegqs STATIC ${JPEGQS_SOURCE})
		target_compile_options(jpegqs PRIVATE ${JPEGQS_COMMON_FLAGS})
		target_include_directories(jpegqs PRIVATE ${JPEGQS_SRC_DIR} ${JPEG_INCLUDE_DIRS})
	endif()

	target_include_directories(jpegqs INTERFACE ${JPEGQS_SRC_DIR})
	set_target_properties(jpegqs PROPERTIES C_CLANG_TIDY "")

	# Vendored libjpeg-turbo: jpeglib.h/jconfig.h only exist after its install step
	if(TARGET libjpeg-turbo-ext)
		foreach(_t jpegqs jpegqs_base jpegqs_sse2 jpegqs_avx2 jpegqs_avx512 jpegqs_dispatch)
			if(TARGET ${_t})
				add_dependencies(${_t} libjpeg-turbo-ext)
			endif()
		endforeach()
	endif()

	add_definitions(-DHAVE_JPEGQS)
	set(JPEGQS_FOUND TRUE)
	message(STATUS "jpeg-quantsmooth: enabled (quality=6, niter=5)")
endif()

# libpng (REQUIRED)
include(CheckCSourceCompiles)
include(CMakePushCheckState)
if(USE_SYSTEM_PNG)
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

	# Prefer APNG-enabled libpng if available
	if(EXISTS "/usr/local/libpng-apng/include/png.h")
		set(PNG_INCLUDE_DIRS "/usr/local/libpng-apng/include")
		set(PNG_LIBRARIES "/usr/local/libpng-apng/lib/libpng16.a")
		message(STATUS "Found APNG-enabled libpng: ${PNG_LIBRARIES}")
	endif()

	# Check if libpng has APNG support. Compile-only probe (static library try_compile):
	# linking would need zlib, and the vendored zlib-ng is a build target, not a file
	cmake_push_check_state(RESET)
	set(CMAKE_REQUIRED_INCLUDES ${PNG_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS})
	set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
	check_c_source_compiles("
		#include <png.h>
		#ifndef PNG_APNG_SUPPORTED
		#error PNG_APNG_SUPPORTED not defined
		#endif
		int probe(void) {
			png_uint_32 num_frames = 0;
			png_uint_32 num_plays = 0;
			png_structp png_ptr = NULL;
			png_infop info_ptr = NULL;
			return (int)png_get_acTL(png_ptr, info_ptr, &num_frames, &num_plays);
		}
	" PNG_APNG_SUPPORTED_CHECK)
	cmake_pop_check_state()
	set(PNG_LIB_TYPE "host")

else()
	# Vendored libpng + APNG patch from the libpng-apng project:
	#   https://sourceforge.net/projects/libpng-apng/files/libpng16/1.6.59/libpng-1.6.59-apng.patch.gz
	#   (gunzipped, sha256 7a2671b644218c0bb98fa17d1efbc653fd4f7d87d42b841a5cd3019208228de5)
	# The submodule stays pristine: it is copied to the build dir and patched there.
	# The copy is re-created whenever the patch or the libpng sources change.
	set(LIBPNG_UPSTREAM_DIR "${CMAKE_SOURCE_DIR}/vendor/libpng")
	set(LIBPNG_APNG_PATCH "${CMAKE_SOURCE_DIR}/vendor/patches/libpng-1.6.59-apng.patch")
	set(LIBPNG_PATCHED_DIR "${CMAKE_BINARY_DIR}/_deps/libpng-src")
	set(LIBPNG_PATCH_STAMP "${CMAKE_BINARY_DIR}/_deps/libpng-src.stamp")
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
		"${LIBPNG_APNG_PATCH}" "${LIBPNG_UPSTREAM_DIR}/png.h" "${LIBPNG_UPSTREAM_DIR}/CMakeLists.txt")

	file(SHA256 "${LIBPNG_APNG_PATCH}" _png_patch_hash)
	file(SHA256 "${LIBPNG_UPSTREAM_DIR}/png.h" _png_h_hash)
	file(SHA256 "${LIBPNG_UPSTREAM_DIR}/CMakeLists.txt" _png_cmake_hash)
	set(_png_stamp_value "${_png_patch_hash}:${_png_h_hash}:${_png_cmake_hash}")
	set(_png_stamp_old "")
	if(EXISTS "${LIBPNG_PATCH_STAMP}")
		file(READ "${LIBPNG_PATCH_STAMP}" _png_stamp_old)
	endif()

	if(NOT _png_stamp_old STREQUAL _png_stamp_value OR NOT EXISTS "${LIBPNG_PATCHED_DIR}/png.h")
		message(STATUS "libpng: preparing APNG-patched copy in ${LIBPNG_PATCHED_DIR}")
		file(REMOVE_RECURSE "${LIBPNG_PATCHED_DIR}")
		file(REMOVE "${LIBPNG_PATCH_STAMP}")
		file(COPY "${LIBPNG_UPSTREAM_DIR}/" DESTINATION "${LIBPNG_PATCHED_DIR}"
			PATTERN ".git" EXCLUDE)
		find_program(PATCH_EXECUTABLE NAMES patch)
		if(PATCH_EXECUTABLE)
			set(_png_patch_cmd "${PATCH_EXECUTABLE}" -p1 --forward --batch -i "${LIBPNG_APNG_PATCH}")
		else()
			find_package(Git REQUIRED)
			set(_png_patch_cmd "${GIT_EXECUTABLE}" apply -p1 --whitespace=nowarn "${LIBPNG_APNG_PATCH}")
		endif()
		execute_process(
			COMMAND ${_png_patch_cmd}
			WORKING_DIRECTORY "${LIBPNG_PATCHED_DIR}"
			RESULT_VARIABLE _png_patch_result
			OUTPUT_VARIABLE _png_patch_output
			ERROR_VARIABLE _png_patch_output)
		if(NOT _png_patch_result EQUAL 0)
			message(FATAL_ERROR "libpng: applying ${LIBPNG_APNG_PATCH} failed:\n${_png_patch_output}")
		endif()
		file(WRITE "${LIBPNG_PATCH_STAMP}" "${_png_stamp_value}")
	endif()

	# pkg_check_modules(PNG ...) from a previous USE_SYSTEM_PNG=ON configure caches
	# PNG_PREFIX=/usr (INTERNAL); libpng would take it as its API symbol prefix option
	get_property(_png_prefix_type CACHE PNG_PREFIX PROPERTY TYPE)
	if(_png_prefix_type STREQUAL "INTERNAL")
		unset(PNG_PREFIX CACHE)
	endif()
	set(PNG_SHARED OFF CACHE BOOL "libpng: no shared library")
	set(PNG_STATIC ON CACHE BOOL "libpng: static library")
	set(PNG_TESTS OFF CACHE BOOL "libpng: no tests")
	set(PNG_TOOLS OFF CACHE BOOL "libpng: no tools")
	set(PNG_FRAMEWORK OFF CACHE BOOL "libpng: no framework bundle")
	set(SKIP_INSTALL_ALL ON CACHE BOOL "libpng: skip install rules")
	imgcat2_vendor_begin()
	add_subdirectory("${LIBPNG_PATCHED_DIR}" "${CMAKE_BINARY_DIR}/_deps/libpng-build" EXCLUDE_FROM_ALL)
	imgcat2_vendor_end()

	set(PNG_FOUND TRUE)
	set(PNG_LIBRARIES png_static)
	set(PNG_INCLUDE_DIRS "${LIBPNG_PATCHED_DIR}" "${CMAKE_BINARY_DIR}/_deps/libpng-build")

	# APNG support is known statically: the patch defines PNG_APNG_SUPPORTED in png.h
	file(STRINGS "${LIBPNG_PATCHED_DIR}/png.h" _png_apng_define REGEX "^#define PNG_APNG_SUPPORTED")
	if(_png_apng_define)
		set(PNG_APNG_SUPPORTED_CHECK TRUE)
	else()
		message(FATAL_ERROR "libpng: APNG patch applied but PNG_APNG_SUPPORTED is not defined in png.h")
	endif()
	set(PNG_LIB_TYPE "vendored libpng 1.6.59 + APNG patch")
endif()
add_definitions(-DHAVE_LIBPNG)

if(PNG_APNG_SUPPORTED_CHECK)
	# PNG_APNG_SUPPORTED is already defined in png.h when APNG support is available
	# Don't redefine it here to avoid macro redefinition warnings
	message(STATUS "libpng has APNG support: YES")

else()
	message(WARNING "libpng does not have APNG support, APNG animations will not be decoded")
endif()

# giflib (optional)
if(ENABLE_GIF)
	if(USE_SYSTEM_GIF)
		find_library(GIF_LIBRARY NAMES gif)
		find_path(GIF_INCLUDE_DIR gif_lib.h)
		set(GIF_LIB_TYPE "host")

	else()
		# Vendored giflib: hand-written static target (upstream only ships a Makefile).
		# Source list mirrors SOURCES in vendor/giflib/Makefile (libgif, no utilities).
		set(GIFLIB_SRC_DIR "${CMAKE_SOURCE_DIR}/vendor/giflib")
		add_library(vendor_giflib STATIC
			${GIFLIB_SRC_DIR}/dgif_lib.c
			${GIFLIB_SRC_DIR}/egif_lib.c
			${GIFLIB_SRC_DIR}/gifalloc.c
			${GIFLIB_SRC_DIR}/gif_err.c
			${GIFLIB_SRC_DIR}/gif_font.c
			${GIFLIB_SRC_DIR}/gif_hash.c
			${GIFLIB_SRC_DIR}/openbsd-reallocarray.c
			${GIFLIB_SRC_DIR}/quantize.c
		)
		target_include_directories(vendor_giflib PUBLIC ${GIFLIB_SRC_DIR})
		set_target_properties(vendor_giflib PROPERTIES
			C_STANDARD 99
			C_EXTENSIONS ON
			POSITION_INDEPENDENT_CODE ON
			C_CLANG_TIDY "")
		set(GIF_LIBRARY vendor_giflib)
		set(GIF_INCLUDE_DIR ${GIFLIB_SRC_DIR})
		set(GIF_LIB_TYPE "vendored giflib 6.1.3")
	endif()

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
if(ENABLE_WEBP AND NOT USE_SYSTEM_WEBP)
	# Vendored libwebp: only the libraries (webp, webpdemux, sharpyuv), no tools/extras
	set(WEBP_LINK_STATIC ON CACHE BOOL "libwebp: static")
	set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "libwebp: no anim utils")
	set(WEBP_BUILD_CWEBP OFF CACHE BOOL "libwebp: no cwebp")
	set(WEBP_BUILD_DWEBP OFF CACHE BOOL "libwebp: no dwebp")
	set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "libwebp: no gif2webp")
	set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "libwebp: no img2webp")
	set(WEBP_BUILD_VWEBP OFF CACHE BOOL "libwebp: no vwebp")
	set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "libwebp: no webpinfo")
	set(WEBP_BUILD_LIBWEBPMUX OFF CACHE BOOL "libwebp: no libwebpmux")
	set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "libwebp: no webpmux")
	set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "libwebp: no extras")
	set(WEBP_BUILD_FUZZTEST OFF CACHE BOOL "libwebp: no fuzztest")
	imgcat2_vendor_begin()
	add_subdirectory(vendor/libwebp EXCLUDE_FROM_ALL)
	imgcat2_vendor_end()
	set(WEBP_FOUND TRUE)
	set(WEBP_LIBRARIES webpdemux webp sharpyuv)
	set(WEBP_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/vendor/libwebp/src")
	set(WEBP_LIB_TYPE "vendored libwebp 1.6.0")
	add_definitions(-DHAVE_WEBP)

elseif(ENABLE_WEBP)
	set(WEBP_LIB_TYPE "host")
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

			if(X265_FOUND)
				find_library(X265_STATIC NAMES libx265.a)
				if(X265_STATIC)
					find_library(NUMA_LIB NAMES numa)
					if(NUMA_LIB)
						list(APPEND HEIF_CUSTOM_LIBS ${X265_STATIC})
						list(APPEND HEIF_CUSTOM_LIBS ${NUMA_LIB})
					endif()

				else()
					message(STATUS "  libx265 found but no static library, skipping")
					set(X265_FOUND FALSE)
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

			# libheif may need brotli for compression
			if(BROTLIDEC_LIB AND BROTLIENC_LIB AND BROTLICOMMON_LIB)
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

		add_definitions(-DHAVE_HEIF)

	else()
		message(WARNING "libheif not found, HEIF support disabled")
		set(ENABLE_HEIF OFF)
	endif()
endif()

# TIFF (optional)
if(ENABLE_TIFF AND NOT USE_SYSTEM_TIFF)
	# Vendored libtiff: codecs = built-ins + zlib (zlib-ng) + JPEG (vendored libjpeg-turbo)
	# + WebP (vendored libwebp when enabled). jbig (GPL), lerc, lzma, zstd, libdeflate,
	# C++ API, tools, tests, docs: OFF. libtiff pins cmake_policy(VERSION 3.10), so its
	# options are pre-seeded as CACHE entries (CMP0077 OLD ignores normal variables).
	set(tiff-static ON CACHE BOOL "libtiff: static")
	set(tiff-tools OFF CACHE BOOL "libtiff: no tools")
	set(tiff-tests OFF CACHE BOOL "libtiff: no tests")
	set(tiff-contrib OFF CACHE BOOL "libtiff: no contrib")
	set(tiff-docs OFF CACHE BOOL "libtiff: no docs")
	set(tiff-install OFF CACHE BOOL "libtiff: no install rules")
	set(tiff-cxx OFF CACHE BOOL "libtiff: no C++ stream API")
	set(tiff-opengl OFF CACHE BOOL "libtiff: no OpenGL viewer")
	set(sphinx OFF CACHE BOOL "libtiff: no sphinx docs")
	set(zlib ON CACHE BOOL "libtiff: Deflate via zlib-ng")
	set(pixarlog ON CACHE BOOL "libtiff: PixarLog (zlib)")
	set(libdeflate OFF CACHE BOOL "libtiff: no libdeflate")
	set(jpeg ON CACHE BOOL "libtiff: JPEG via vendored libjpeg-turbo")
	set(old-jpeg ON CACHE BOOL "libtiff: Old JPEG (read-only)")
	set(jpeg12 OFF CACHE BOOL "libtiff: no separate 12-bit libjpeg")
	set(jbig OFF CACHE BOOL "libtiff: no JBIG (GPL)")
	set(lerc OFF CACHE BOOL "libtiff: no LERC")
	set(lzma OFF CACHE BOOL "libtiff: no LZMA")
	set(zstd OFF CACHE BOOL "libtiff: no ZSTD")
	if(WEBP_FOUND AND TARGET webp)
		set(webp ON CACHE BOOL "libtiff: WebP via vendored libwebp")
		# libtiff's own FindWebP: resolve to the vendored target, not a host libwebp
		set(WebP_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/vendor/libwebp/src" CACHE PATH "libwebp include dir (vendored)" FORCE)
		set(WebP_LIBRARY webp CACHE STRING "libwebp library (vendored target)" FORCE)
		if(NOT TARGET WebP::webp)
			add_library(WebP::webp ALIAS webp)
		endif()
	else()
		set(webp OFF CACHE BOOL "libtiff: no WebP")
	endif()

	# libtiff links JPEG::JPEG by name; the host pkg-config path (USE_SYSTEM_JPEG) has none
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
	# link at configure time because the vendored library is built later
	if(TARGET libjpeg-turbo-ext)
		set(HAVE_JPEGTURBO_DUAL_MODE_8 1 CACHE INTERNAL "vendored libjpeg-turbo: jpeg_read_scanlines")
		set(HAVE_JPEGTURBO_DUAL_MODE_12 1 CACHE INTERNAL "vendored libjpeg-turbo: jpeg12_read_scanlines")
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
	set(TIFF_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/vendor/libtiff/libtiff" "${CMAKE_BINARY_DIR}/vendor/libtiff/libtiff")
	set(TIFF_LIB_TYPE "vendored libtiff 4.7.2")
	add_definitions(-DHAVE_TIFF)

elseif(ENABLE_TIFF)
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
		add_definitions(-DHAVE_TIFF)
	else()
		message(WARNING "libtiff not found, TIFF support disabled")
		set(ENABLE_TIFF OFF)
	endif()
endif()

# RAW (optional)
if(ENABLE_RAW AND NOT USE_SYSTEM_RAW)
	# LibRaw is C++ (only needed when the vendored copy is built)
	enable_language(CXX)

	# Little-CMS 2 (LibRaw color management). License: MIT.
	if(USE_SYSTEM_LCMS2)
		pkg_check_modules(LCMS2 REQUIRED lcms2)
		find_library(LCMS2_STATIC_LIB NAMES liblcms2.a lcms2 REQUIRED)
		add_library(vendor_lcms2_host INTERFACE)
		target_include_directories(vendor_lcms2_host INTERFACE ${LCMS2_INCLUDE_DIRS})
		target_link_libraries(vendor_lcms2_host INTERFACE ${LCMS2_STATIC_LIB} m)
		set(LCMS2_TARGET vendor_lcms2_host)
		set(LCMS2_LIB_TYPE "host")
	else()
		# Pattern B: hand-written static target from src/*.c
		set(LCMS2_SRC_DIR "${CMAKE_SOURCE_DIR}/vendor/lcms2")
		file(GLOB LCMS2_SOURCES "${LCMS2_SRC_DIR}/src/*.c")
		add_library(vendor_lcms2 STATIC ${LCMS2_SOURCES})
		target_include_directories(vendor_lcms2
			PUBLIC ${LCMS2_SRC_DIR}/include
			PRIVATE ${LCMS2_SRC_DIR}/src)
		set_target_properties(vendor_lcms2 PROPERTIES
			POSITION_INDEPENDENT_CODE ON
			C_CLANG_TIDY "")
		if(UNIX)
			target_link_libraries(vendor_lcms2 PUBLIC m)
		endif()
		set(LCMS2_TARGET vendor_lcms2)
		set(LCMS2_LIB_TYPE "vendored lcms2 2.20")
	endif()

	# LibRaw: pattern B (hand-written static target). Chosen over the LibRaw-cmake wrapper:
	# one pinned submodule instead of two (LibRaw-cmake has no release tags), and the
	# source list is simply upstream's Makefile.dist LIB_OBJECTS = src/**/*.cpp minus the
	# *_ph.cpp placeholder units (79 files). Thread-safe variant (libraw_r equivalent).
	# License: LibRaw is dual-licensed LGPL-2.1 or CDDL-1.0; imgcat2 uses it under
	# CDDL-1.0 (file-level copyleft, no LGPL relinking requirement for a static binary).
	set(LIBRAW_SRC_DIR "${CMAKE_SOURCE_DIR}/vendor/libraw")
	file(GLOB LIBRAW_SOURCES "${LIBRAW_SRC_DIR}/src/*.cpp" "${LIBRAW_SRC_DIR}/src/*/*.cpp")
	list(FILTER LIBRAW_SOURCES EXCLUDE REGEX "_ph\\.cpp$")
	add_library(vendor_libraw STATIC ${LIBRAW_SOURCES})
	target_include_directories(vendor_libraw PUBLIC ${LIBRAW_SRC_DIR})
	# USE_LCMS2 is PUBLIC: libraw_types.h includes lcms2.h under it (header consistency)
	target_compile_definitions(vendor_libraw
		PUBLIC USE_LCMS2
		PRIVATE USE_ZLIB USE_JPEG)
	if(TARGET libjpeg-turbo-ext)
		# jpeg8 ABI: jpeg_mem_src() is available (lossy DNG support)
		target_compile_definitions(vendor_libraw PRIVATE USE_JPEG8)
	endif()
	# Upstream builds LibRaw with -w; its warnings are not ours to fix
	target_compile_options(vendor_libraw PRIVATE -w)
	target_link_libraries(vendor_libraw PUBLIC ${LCMS2_TARGET} ${JPEG_LIBRARIES} ${ZLIB_LIBRARIES})
	set_target_properties(vendor_libraw PROPERTIES
		POSITION_INDEPENDENT_CODE ON
		CXX_CLANG_TIDY "")

	find_package(Threads REQUIRED)
	target_link_libraries(vendor_libraw PUBLIC Threads::Threads)

	# OpenMP, as with the previous host libraw (-fopenmp)
	find_package(OpenMP COMPONENTS CXX)
	if(OpenMP_CXX_FOUND)
		if(UNIX AND NOT APPLE)
			# Flag form so the gcc driver picks static libgomp under -static
			separate_arguments(_raw_omp_flags UNIX_COMMAND "${OpenMP_CXX_FLAGS}")
			target_compile_options(vendor_libraw PRIVATE ${_raw_omp_flags})
			target_link_options(vendor_libraw INTERFACE ${_raw_omp_flags})
		else()
			target_link_libraries(vendor_libraw PUBLIC OpenMP::OpenMP_CXX)
		endif()
		set(RAW_OPENMP "OpenMP")
	else()
		set(RAW_OPENMP "no OpenMP")
	endif()

	set(RAW_FOUND TRUE)
	set(RAW_LIBRARIES vendor_libraw)
	set(RAW_CUSTOM_LIBS vendor_libraw)
	set(RAW_INCLUDE_DIRS ${LIBRAW_SRC_DIR})
	set(RAW_LIB_TYPE "vendored LibRaw 0.22.3, ${RAW_OPENMP}; lcms2: ${LCMS2_LIB_TYPE}")
	add_definitions(-DHAVE_RAW)

elseif(ENABLE_RAW)
	set(RAW_LIB_TYPE "host")
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
			imgcat2_strip_vendored_ldflags(RAW_STATIC_LDFLAGS)
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

			# libraw on macOS (Homebrew) is compiled with OpenMP; add libomp
			find_library(OMP_LIB NAMES omp libomp
				PATHS /opt/homebrew/opt/libomp/lib /usr/local/opt/libomp/lib
				NO_DEFAULT_PATH)
			if(OMP_LIB)
				list(APPEND RAW_CUSTOM_LIBS ${OMP_LIB})
				message(STATUS "  libomp found and added for libraw: ${OMP_LIB}")
			else()
				message(WARNING "libomp not found; libraw may fail to link (OpenMP required)")
			endif()
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

# resvg (optional, preferred SVG decoder)
if(ENABLE_RESVG)
	# Find static library explicitly on macOS
	if(APPLE)
		find_library(RESVG_LIBRARY NAMES resvg)
		find_path(RESVG_INCLUDE_DIR resvg/resvg.h)
		if(RESVG_LIBRARY AND RESVG_INCLUDE_DIR)
			set(RESVG_LIBRARIES ${RESVG_LIBRARY})
			set(RESVG_INCLUDE_DIRS ${RESVG_INCLUDE_DIR})
			set(RESVG_FOUND TRUE)
			# Get version from pkg-config if available
			execute_process(COMMAND pkg-config --modversion resvg
				OUTPUT_VARIABLE RESVG_VERSION
				OUTPUT_STRIP_TRAILING_WHITESPACE
				ERROR_QUIET)
		endif()

	else()
		pkg_check_modules(RESVG resvg)
	endif()

	if(RESVG_FOUND)
		add_definitions(-DHAVE_RESVG)
		message(STATUS "resvg found: ${RESVG_VERSION} (preferred SVG decoder)")

	else()
		message(STATUS "resvg not found, using nanosvg fallback")
		set(ENABLE_RESVG OFF)
	endif()
endif()

# Vendored providers linked after every host static library (libheif stack, libjxl, and
# libtiff/libraw when USE_SYSTEM_*): those reference jpeg/webp/lcms2/zlib symbols, and with
# static linking the provider must come after its user on the link line.
# Archive paths (not target names) on purpose: CMake de-duplicates targets in the
# transitive link closure (imgcat2_lib -> tests) and would drop the trailing copy, while
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
list(APPEND IMGCAT2_VENDORED_TRAILING_LIBS "$<TARGET_FILE:ZLIB::ZLIB>")

# Project-only compile settings
# Everything declared above (vendored dependencies) is created without these; every
# target created below (imgcat2, imgcat2_lib, src/tests) gets -Werror and clang-tidy.
add_compile_options(${IMGCAT2_WARNING_FLAGS})
if(IMGCAT2_CLANG_TIDY)
	set(CMAKE_C_CLANG_TIDY "${IMGCAT2_CLANG_TIDY}")
endif()

# Fully static Linux link: when a C++ dependency (vendored LibRaw) makes C++ the linker
# language, CMake appends the C compiler's implicit libs, which include the shared-only
# libgcc_s (no libgcc_s.a exists). The compiler driver handles libgcc itself under -static.
if(UNIX AND NOT APPLE)
	foreach(_lang C CXX)
		if(CMAKE_${_lang}_IMPLICIT_LINK_LIBRARIES)
			list(REMOVE_ITEM CMAKE_${_lang}_IMPLICIT_LINK_LIBRARIES gcc_s)
		endif()
	endforeach()
endif()

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

if(RESVG_FOUND)
	include_directories(${RESVG_INCLUDE_DIRS})
endif()

if(JPEGQS_FOUND)
	include_directories(${CMAKE_SOURCE_DIR}/vendor/jpeg-quantsmooth)
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
	src/imgcat2/decoders/decoder_pnm.c
	src/imgcat2/decoders/decoder_yuv.c
	src/imgcat2/decoders/decoder_nanosvg.c

	# Encoders module
	src/imgcat2/encoders/encoder.c
	src/imgcat2/encoders/encoder_jpeg.c
	src/imgcat2/encoders/encoder_png.c

	# Terminal module - file renderer
	src/imgcat2/terminal/file.c

	# Text/Font module
	src/imgcat2/text/font_manager.c
	src/imgcat2/font/embedded_fonts.c

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

if(RESVG_FOUND)
	list(APPEND IMGCAT2_SOURCES src/imgcat2/decoders/decoder_resvg.c)
endif()

# Conditional encoder sources
if(WEBP_FOUND)
	list(APPEND IMGCAT2_SOURCES src/imgcat2/encoders/encoder_webp.c)
endif()

if(HEIF_FOUND)
	list(APPEND IMGCAT2_SOURCES src/imgcat2/encoders/encoder_heif.c)
endif()

if(JXL_FOUND)
	list(APPEND IMGCAT2_SOURCES src/imgcat2/encoders/encoder_jxl.c)
endif()

if(ENABLE_EXIF_READER)
	list(APPEND IMGCAT2_SOURCES src/imgcat2/metadata/xml_parser.c)
	list(APPEND IMGCAT2_SOURCES src/imgcat2/metadata/exif_reader.c)
	add_definitions(-DHAVE_EXIF_READER)
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

# Font embedding
add_custom_command(
	OUTPUT ${CMAKE_SOURCE_DIR}/src/imgcat2/font/embedded_fonts.c
	       ${CMAKE_SOURCE_DIR}/src/imgcat2/font/embedded_fonts.h
	COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/embed_font.py
	        ${CMAKE_SOURCE_DIR}/src/imgcat2/font
	        ${CMAKE_SOURCE_DIR}/src/imgcat2/font/embedded_fonts
	DEPENDS ${CMAKE_SOURCE_DIR}/src/imgcat2/font/dejavu-sans-mono.book.ttf
	        ${CMAKE_SOURCE_DIR}/src/imgcat2/font/dejavu-sans-mono.bold.ttf
	        ${CMAKE_SOURCE_DIR}/src/imgcat2/font/dejavu-sans-mono.oblique.ttf
	        ${CMAKE_SOURCE_DIR}/src/imgcat2/font/dejavu-sans-mono.bold-oblique.ttf
	        ${CMAKE_SOURCE_DIR}/tools/embed_font.py
	COMMENT "Embedding DejaVu Sans Mono font family (Regular, Bold, Italic, Bold-Italic)"
)

add_custom_target(embed_fonts ALL DEPENDS
	${CMAKE_SOURCE_DIR}/src/imgcat2/font/embedded_fonts.c
	${CMAKE_SOURCE_DIR}/src/imgcat2/font/embedded_fonts.h
)

# Main executable
add_executable(imgcat2 ${IMGCAT2_SOURCES} src/imgcat2/main.c)
add_dependencies(imgcat2 embed_fonts)

# Library for tests (without main.c)
add_library(imgcat2_lib STATIC ${IMGCAT2_SOURCES})
add_dependencies(imgcat2_lib embed_fonts)

# Link libraries for executable
target_link_libraries(imgcat2 PRIVATE
	${PNG_LIBRARIES}
	${ZLIB_LIBRARIES}
	$<$<BOOL:${JPEGQS_FOUND}>:jpegqs>
	${JPEG_LIBRARIES}
)

# Link libraries for library
target_link_libraries(imgcat2_lib PUBLIC
	${PNG_LIBRARIES}
	${ZLIB_LIBRARIES}
	$<$<BOOL:${JPEGQS_FOUND}>:jpegqs>
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
	if(NOT USE_SYSTEM_TIFF)
		# Vendored libtiff target (carries zlib-ng/jpeg/webp as link dependencies)
		target_link_libraries(imgcat2 PRIVATE ${TIFF_LIBRARIES})
		target_link_libraries(imgcat2_lib PUBLIC ${TIFF_LIBRARIES})
	elseif(APPLE)
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
	if(NOT USE_SYSTEM_RAW)
		# Vendored LibRaw target (carries lcms2/jpeg/zlib/OpenMP as link dependencies)
		target_link_libraries(imgcat2 PRIVATE ${RAW_LIBRARIES})
		target_link_libraries(imgcat2_lib PUBLIC ${RAW_LIBRARIES})
	elseif(APPLE)
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

if(RESVG_FOUND)
	target_link_libraries(imgcat2 PRIVATE ${RESVG_LIBRARIES})
	target_link_libraries(imgcat2_lib PUBLIC ${RESVG_LIBRARIES})
endif()

# Vendored providers last (see IMGCAT2_VENDORED_TRAILING_LIBS)
target_link_libraries(imgcat2 PRIVATE ${IMGCAT2_VENDORED_TRAILING_LIBS})
target_link_libraries(imgcat2_lib PUBLIC ${IMGCAT2_VENDORED_TRAILING_LIBS})

# Platform-specific libraries
if(UNIX AND NOT APPLE)
	target_link_libraries(imgcat2 PUBLIC m)  # Math library
	target_link_libraries(imgcat2_lib PUBLIC m)  # Math library
endif()

# C++ standard library on macOS (static or dynamic)
if(APPLE)
	if(STATIC_LIBCXX_LIBS)
		# Use static libc++
		target_link_libraries(imgcat2 PRIVATE ${STATIC_LIBCXX_LIBS})
		target_link_libraries(imgcat2_lib PUBLIC ${STATIC_LIBCXX_LIBS})
	elseif(DYNAMIC_LIBCXX_LIBS)
		# Use dynamic libc++
		target_link_libraries(imgcat2 PRIVATE ${DYNAMIC_LIBCXX_LIBS})
		target_link_libraries(imgcat2_lib PUBLIC ${DYNAMIC_LIBCXX_LIBS})
	endif()
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
message(STATUS "  libpng: ${PNG_LIBRARIES} (${PNG_LIB_TYPE})")
if(PNG_APNG_SUPPORTED_CHECK)
	message(STATUS "    APNG support: YES")
else()
	message(STATUS "    APNG support: NO (requires libpng >= 1.6.0 with --enable-apng)")
endif()
message(STATUS "  Compression: ${COMPRESSION_LIB_TYPE}")
message(STATUS "    Libraries: ${ZLIB_LIBRARIES}")
message(STATUS "  libjpeg: ${JPEG_LIBRARIES} (${JPEG_LIB_TYPE})")
message(STATUS "")
message(STATUS "Optional Dependencies:")
if(GIF_FOUND)
	message(STATUS "  giflib: ${GIF_LIBRARY} (${GIF_LIB_TYPE})")
endif()
if(WEBP_FOUND)
	message(STATUS "  libwebp: ${WEBP_LIBRARIES} (${WEBP_LIB_TYPE})")
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
		if(KVAZAAR_FOUND)
			list(APPEND HEIF_CODECS "kvazaar")
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
	if(NOT USE_SYSTEM_TIFF)
		message(STATUS "  libtiff: ${TIFF_LIBRARIES} (${TIFF_LIB_TYPE})")
	elseif(UNIX AND NOT APPLE)
		message(STATUS "  libtiff: ${TIFF_STATIC_LDFLAGS}")
	else()
		message(STATUS "  libtiff: ${TIFF_LIBRARIES}")
	endif()
endif()
if(RAW_FOUND)
	if(NOT USE_SYSTEM_RAW)
		message(STATUS "  libraw: ${RAW_LIBRARIES} (${RAW_LIB_TYPE})")
	elseif(UNIX AND NOT APPLE)
		message(STATUS "  libraw: ${RAW_STATIC_LDFLAGS}")
	else()
		message(STATUS "  libraw: ${RAW_LDFLAGS}")
	endif()
endif()
if(NOT GIF_FOUND AND NOT WEBP_FOUND AND NOT HEIF_FOUND AND NOT TIFF_FOUND AND NOT RAW_FOUND)
	message(STATUS "  (none)")
endif()
message(STATUS "")
if(ENABLE_RESVG AND RESVG_FOUND)
	message(STATUS "SVG Decoder: resvg ${RESVG_VERSION}")
else()
	message(STATUS "SVG Decoder: nanosvg")
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
message(STATUS "Metadata Support:")
message(STATUS "  EXIF/XMP: ${ENABLE_EXIF_READER}")
message(STATUS "")
message(STATUS "Other Options:")
message(STATUS "  Testing: ${BUILD_TESTING}")
message(STATUS "  clang-tidy: ${ENABLE_CLANG_TIDY}")
message(STATUS "  clang-format: ${ENABLE_CLANG_FORMAT}")
if(APPLE)
	if(STATIC_LIBCXX_LIBS)
		message(STATUS "  libc++ linking: static")
	else()
		message(STATUS "  libc++ linking: dynamic")
	endif()
endif()
message(STATUS "")
