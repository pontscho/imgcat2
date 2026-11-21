cmake_minimum_required(VERSION 3.21)
project(img2 VERSION 1.0.0 LANGUAGES C)

# Set default build type to RelWithDebInfo if not specified
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the type of build (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
endif()

# Export compile_commands.json for clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)  # GNU extensions for gnu11

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")   # never add cmake/vendored-find here

include(Img2Options)
include(Img2Platform)
include(Img2Tooling)

# Compiler flags
# Optimization flags apply to everything (vendored dependencies included).
# Warning flags (-Werror) are project-only: IMGCAT2_WARNING_FLAGS, applied per target by
# img2_apply_project_settings() (cmake/Img2ProjectSettings.cmake).
add_compile_options(-O3 -g)
set(IMGCAT2_WARNING_FLAGS -Wall -Wextra -Werror -Wno-error=unused-but-set-variable)

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

# Library, command line tool
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

include(Img2Summary)

if(NOT PROJECT_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	message(FATAL_ERROR "img2: PROJECT_SOURCE_DIR clobbered by an include()d module")
endif()
