# Purpose: Vendored zlib-ng (zlib-compat mode): single point of truth for zlib.
# Inputs:  vendor/zlib-ng.
# Outputs: ZLIB::ZLIB, ZLIB_LIBRARIES, ZLIB_INCLUDE_DIRS, ZLIB_INCLUDE_DIR/ZLIB_LIBRARY, COMPRESSION_LIB_TYPE, img2_feature(ZLIB).

# Compression library: vendored zlib-ng (zlib-compat mode). Knobs are normal variables (the vendor section
# sets CMAKE_POLICY_DEFAULT_CMP0077 NEW, BUILD_SHARED_LIBS OFF), so nothing lands in a consumer's cache.
set(ZLIB_COMPAT ON)                 # zlib-compatible API
set(ZLIB_ENABLE_TESTS OFF)          # no tests
set(ZLIBNG_ENABLE_TESTS OFF)        # no tests
set(WITH_GTEST OFF)                 # no gtest
set(WITH_BENCHMARKS OFF)            # no benchmarks
set(WITH_OPTIM ON)                  # optimizations
set(ZLIB_ALIASES ON)                # zlib compatible CMake targets
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
# (normal variables: they count as DEFINED/set for FindZLIB, CMP0125 NEW for its find_*)
set(ZLIB_INCLUDE_DIR "${PROJECT_BINARY_DIR}/vendor/zlib-ng")
set(ZLIB_LIBRARY ZLIB::ZLIB)
set(COMPRESSION_LIB_TYPE "zlib-ng (vendored)")
img2_feature(ZLIB LIBS ${ZLIB_LIBRARIES} INCLUDE_DIRS ${ZLIB_INCLUDE_DIRS})
