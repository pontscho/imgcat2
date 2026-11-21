# Purpose: Brotli: single vendored copy shared by libheif and libjxl.
# Inputs:  IMG2_ENABLE_HEIF, IMG2_ENABLE_JXL, IMG2_USE_SYSTEM_{HEIF,JXL}.
# Outputs: brotli* targets, IMGCAT2_VENDORED_BROTLI_INCLUDE_DIRS/LIBRARIES, BROTLI_LIB_TYPE.

# Brotli (MIT): a single vendored copy shared by libheif (brotli-compressed items of the
# ISO/IEC 23001-17 uncompressed codec) and libjxl. Static libraries only, no CLI/tests.
if((IMG2_ENABLE_HEIF AND NOT IMG2_USE_SYSTEM_HEIF) OR (IMG2_ENABLE_JXL AND NOT IMG2_USE_SYSTEM_JXL))
	set(BROTLI_BUILD_TOOLS OFF CACHE BOOL "brotli: no CLI tool")
	set(BROTLI_BUILD_FOR_PACKAGE OFF CACHE BOOL "brotli: static libraries only")
	set(BROTLI_DISABLE_TESTS ON CACHE BOOL "brotli: no tests")
	imgcat2_vendor_begin(NO_INSTALL)
	add_subdirectory(vendor/brotli EXCLUDE_FROM_ALL)
	imgcat2_vendor_end()
	set(IMGCAT2_VENDORED_BROTLI_LIBRARIES brotlidec brotlienc brotlicommon)
	set(IMGCAT2_VENDORED_BROTLI_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/vendor/brotli/c/include")
	set(BROTLI_LIB_TYPE "vendored brotli 1.2.1")
endif()
