cmake_minimum_required(VERSION 3.21)
project(img2 VERSION 1.0.0 LANGUAGES C)

# Default build type RelWithDebInfo, only as the top-level project (a consumer's build type is its own)
if(PROJECT_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the type of build (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
endif()

# Export compile_commands.json for clang-tidy (top-level builds only)
if(PROJECT_IS_TOP_LEVEL)
	set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)  # GNU extensions for gnu11

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")   # never add cmake/vendored-find here

include(Img2Options)
include(Img2Platform)
include(Img2Tooling)

# Compiler flags
# No global flags: -O3 -g reach vendored dependencies through the vendor section
# (img2_vendor_section_begin/end) and our own targets through img2_apply_project_settings().
# Warning flags (-Werror only with IMG2_WERROR) are project-only: IMGCAT2_WARNING_FLAGS, applied per target by
# img2_apply_project_settings() (cmake/Img2ProjectSettings.cmake).
if(IMG2_WERROR)
	set(IMGCAT2_WARNING_FLAGS -Wall -Wextra -Werror -Wno-error=unused-but-set-variable)
else()
	set(IMGCAT2_WARNING_FLAGS -Wall -Wextra -Wno-error=unused-but-set-variable)
endif()

# pkg-config
find_package(PkgConfig REQUIRED)

include(Img2VendorHelpers)
include(Img2Features)
img2_vendor_section_begin()   # vendors see the executable link flags (-static, stack size) as before
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorZlib.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorJpeg.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorJpegqs.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorPng.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorGif.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorWebp.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorLcms2.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorBrotli.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorHeif.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorTiff.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorRaw.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorJxl.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorHeaderOnly.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorResvg.cmake")

# Built-in EXIF/XMP metadata parser (no external library)
if(IMG2_ENABLE_EXIF_READER)
	img2_feature(EXIF_READER)
endif()

include(Img2LinkOrder)
img2_vendor_section_end()
include(Img2ProjectSettings)
include(Img2Testing)

# After img2_vendor_section_end(): the -static vendor link flags must not bias the Threads checks
find_package(Threads REQUIRED)

# Fat archive inputs and link interface ([R16] variables; target img2_fat when IMG2_FAT_ARCHIVE)
include(Img2FatArchive)
img2_add_fat_archive()

# Library (+ img2_bundled), command line tool
add_subdirectory(src/libimg2)
if(IMG2_BUILD_CLI OR IMG2_BUILD_TESTS)
	add_subdirectory(src/imgcat2)
endif()

# clang-format target
img2_add_format_target()

# Testing
if(IMG2_BUILD_TESTS)
	enable_testing()
	add_subdirectory(src/tests)
endif()

# Install rules and the img2Config.cmake package (IMG2_INSTALL)
include(Img2Install)

include(Img2Summary)

if(NOT PROJECT_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	message(FATAL_ERROR "img2: PROJECT_SOURCE_DIR clobbered by an include()d module")
endif()
