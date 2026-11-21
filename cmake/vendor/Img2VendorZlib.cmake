# Purpose: Vendored zlib-ng (zlib-compat mode): single point of truth for zlib.
# Inputs:  vendor/zlib-ng.
# Outputs: ZLIB::ZLIB, ZLIB_LIBRARIES, ZLIB_INCLUDE_DIRS, ZLIB_INCLUDE_DIR/ZLIB_LIBRARY cache, COMPRESSION_LIB_TYPE.

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
set(ZLIB_INCLUDE_DIRS "${PROJECT_BINARY_DIR}/vendor/zlib-ng" "${PROJECT_SOURCE_DIR}/vendor/zlib-ng")
# Pre-seed FindZLIB so find_package(ZLIB) inside vendored subprojects resolves to zlib-ng
# (zlib.h/zconf.h are generated into the zlib-ng binary dir) instead of the host libz
set(ZLIB_INCLUDE_DIR "${PROJECT_BINARY_DIR}/vendor/zlib-ng" CACHE PATH "zlib include dir (vendored zlib-ng)" FORCE)
set(ZLIB_LIBRARY ZLIB::ZLIB CACHE STRING "zlib library (vendored zlib-ng target)" FORCE)
set(COMPRESSION_LIB_TYPE "zlib-ng (vendored)")
