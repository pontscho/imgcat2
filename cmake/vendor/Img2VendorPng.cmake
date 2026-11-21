# Purpose: libpng (required): vendored + APNG patch, or host.
# Inputs:  IMG2_USE_SYSTEM_PNG, ZLIB_*.
# Outputs: PNG_FOUND, PNG_LIBRARIES, PNG_INCLUDE_DIRS, PNG_LIB_TYPE, PNG_APNG_SUPPORTED_CHECK, img2_feature(LIBPNG).

# libpng (REQUIRED)
include(CheckCSourceCompiles)
include(CMakePushCheckState)
if(IMG2_USE_SYSTEM_PNG)
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
	set(_png_feature_args SYSTEM_LIBS ${PNG_LIBRARIES})
	img2_warn_host_dependency("libpng (IMG2_USE_SYSTEM_PNG)")

else()
	# Vendored libpng + APNG patch from the libpng-apng project:
	#   https://sourceforge.net/projects/libpng-apng/files/libpng16/1.6.59/libpng-1.6.59-apng.patch.gz
	#   (gunzipped, sha256 7a2671b644218c0bb98fa17d1efbc653fd4f7d87d42b841a5cd3019208228de5)
	# The submodule stays pristine: it is copied to the build dir and patched there.
	# The copy is re-created whenever the patch or the libpng sources change.
	set(LIBPNG_UPSTREAM_DIR "${PROJECT_SOURCE_DIR}/vendor/libpng")
	set(LIBPNG_APNG_PATCH "${PROJECT_SOURCE_DIR}/vendor/patches/libpng-1.6.59-apng.patch")
	set(LIBPNG_PATCHED_DIR "${PROJECT_BINARY_DIR}/_deps/libpng-src")
	set(LIBPNG_PATCH_STAMP "${PROJECT_BINARY_DIR}/_deps/libpng-src.stamp")
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

	# pkg_check_modules(PNG ...) from a previous IMG2_USE_SYSTEM_PNG=ON configure caches
	# PNG_PREFIX=/usr (INTERNAL); libpng would take it as its API symbol prefix option
	get_property(_png_prefix_type CACHE PNG_PREFIX PROPERTY TYPE)
	if(_png_prefix_type STREQUAL "INTERNAL")
		unset(PNG_PREFIX CACHE)
	endif()
	# Knobs as normal variables (vendor section: CMAKE_POLICY_DEFAULT_CMP0077 NEW)
	set(PNG_SHARED OFF)         # no shared library
	set(PNG_STATIC ON)          # static library
	set(PNG_TESTS OFF)          # no tests
	set(PNG_TOOLS OFF)          # no tools
	set(PNG_FRAMEWORK OFF)      # no framework bundle
	set(SKIP_INSTALL_ALL ON)    # skip install rules
	imgcat2_vendor_begin()
	add_subdirectory("${LIBPNG_PATCHED_DIR}" "${PROJECT_BINARY_DIR}/_deps/libpng-build" EXCLUDE_FROM_ALL)
	imgcat2_vendor_end()

	set(PNG_FOUND TRUE)
	set(PNG_LIBRARIES png_static)
	set(PNG_INCLUDE_DIRS "${LIBPNG_PATCHED_DIR}" "${PROJECT_BINARY_DIR}/_deps/libpng-build")

	# APNG support is known statically: the patch defines PNG_APNG_SUPPORTED in png.h
	file(STRINGS "${LIBPNG_PATCHED_DIR}/png.h" _png_apng_define REGEX "^#define PNG_APNG_SUPPORTED")
	if(_png_apng_define)
		set(PNG_APNG_SUPPORTED_CHECK TRUE)
	else()
		message(FATAL_ERROR "libpng: APNG patch applied but PNG_APNG_SUPPORTED is not defined in png.h")
	endif()
	set(PNG_LIB_TYPE "vendored libpng 1.6.59 + APNG patch")
	set(_png_feature_args ARCHIVES "$<TARGET_FILE:png_static>" ARCHIVE_TARGETS png_static)
endif()
img2_feature(LIBPNG LIBS ${PNG_LIBRARIES} INCLUDE_DIRS ${PNG_INCLUDE_DIRS} ${_png_feature_args})

if(PNG_APNG_SUPPORTED_CHECK)
	# PNG_APNG_SUPPORTED is already defined in png.h when APNG support is available
	# Don't redefine it here to avoid macro redefinition warnings
	message(STATUS "libpng has APNG support: YES")

else()
	message(WARNING "libpng does not have APNG support, APNG animations will not be decoded")
endif()
