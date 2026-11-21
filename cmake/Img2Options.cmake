# Purpose: Build options of libimg2 / imgcat2 (codec switches, vendored vs host deps).
# Inputs:  PROJECT_IS_TOP_LEVEL.
# Outputs: IMG2_ENABLE_*, IMG2_USE_SYSTEM_*, IMG2_BUILD_TESTS, IMG2_BUILD_CLI, IMG2_WERROR,
#          IMG2_POSITION_INDEPENDENT_CODE cache options.

# Build options
option(IMG2_ENABLE_GIF "Enable GIF support (giflib)" ON)
option(IMG2_ENABLE_WEBP "Enable WebP support" ON)
option(IMG2_ENABLE_HEIF "Enable HEIF support" ON)
option(IMG2_ENABLE_TIFF "Enable TIFF support" ON)
option(IMG2_ENABLE_RAW "Enable RAW support (libraw)" ON)
option(IMG2_ENABLE_JXL "Enable JPEG XL support (libjxl)" ON)
option(IMG2_ENABLE_RESVG "Enable resvg SVG renderer (preferred over nanosvg)" OFF)
option(IMG2_ENABLE_EXIF_READER "Enable EXIF/XMP metadata parsing (built-in)" ON)
option(IMG2_BUILD_TESTS "Build img2 tests" ${PROJECT_IS_TOP_LEVEL})
option(IMG2_BUILD_CLI "Build the imgcat2 command line tool" ${PROJECT_IS_TOP_LEVEL})
option(IMG2_ENABLE_JPEGQS "Enable JPEG quantsmooth (enhances JPEG decoding quality)" ON)
option(IMG2_ENABLE_CLANG_TIDY "Enable clang-tidy static analysis" OFF)
option(IMG2_ENABLE_CLANG_FORMAT "Enable clang-format code formatting" ON)
option(IMG2_ENABLE_STATIC_LIBCXX "Enable static linking of libc++ on macOS" OFF)
option(IMG2_WERROR "Treat warnings as errors" ${PROJECT_IS_TOP_LEVEL})
option(IMG2_POSITION_INDEPENDENT_CODE "Build libimg2 and bundled vendors as PIC (needed for PIE/shared consumers)" ON)

# Dependency source selection: OFF (default) = build the vendored copy from vendor/,
# ON = use the host library found via pkg-config / find_library (legacy behaviour)
option(IMG2_USE_SYSTEM_JPEG "Use host libjpeg(-turbo) instead of vendored libjpeg-turbo" OFF)
option(IMG2_USE_SYSTEM_PNG "Use host libpng instead of vendored libpng (+APNG patch)" OFF)
option(IMG2_USE_SYSTEM_GIF "Use host giflib instead of vendored giflib" OFF)
option(IMG2_USE_SYSTEM_WEBP "Use host libwebp instead of vendored libwebp" OFF)
option(IMG2_USE_SYSTEM_TIFF "Use host libtiff instead of vendored libtiff" OFF)
option(IMG2_USE_SYSTEM_LCMS2 "Use host lcms2 instead of vendored Little-CMS" OFF)
option(IMG2_USE_SYSTEM_RAW "Use host libraw instead of vendored LibRaw" OFF)
option(IMG2_USE_SYSTEM_HEIF "Use host libheif (+ codecs) instead of vendored libheif/libde265/kvazaar/aom/openjpeg" OFF)
option(IMG2_USE_SYSTEM_JXL "Use host libjxl (+ highway/brotli) instead of vendored libjxl" OFF)
