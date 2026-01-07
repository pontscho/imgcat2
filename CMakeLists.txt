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
option(BUILD_TESTING "Build tests" ON)
option(ENABLE_CLANG_TIDY "Enable clang-tidy static analysis" ON)
option(ENABLE_CLANG_FORMAT "Enable clang-format code formatting" ON)

# Static linking preference
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)

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
	find_package(ZLIB REQUIRED)
	set(ZLIB_LIBRARIES ${ZLIB_LIBRARIES})
	set(ZLIB_INCLUDE_DIRS ${ZLIB_INCLUDE_DIRS})
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
pkg_check_modules(PNG libpng16)
if(NOT PNG_FOUND)
	pkg_check_modules(PNG libpng)
endif()

if(NOT PNG_FOUND)
	message(FATAL_ERROR "libpng is required but not found")
endif()
add_definitions(-DHAVE_LIBPNG)

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
	pkg_check_modules(WEBP libwebp)
	if(WEBP_FOUND)
		add_definitions(-DENABLE_WEBP)
	else()
		message(WARNING "libwebp not found, WebP support disabled")
		set(ENABLE_WEBP OFF)
	endif()
endif()

# HEIF (optional)
if(ENABLE_HEIF)
	pkg_check_modules(HEIF libheif)
	if(HEIF_FOUND)
		add_definitions(-DENABLE_HEIF)
	else()
		message(WARNING "libheif not found, HEIF support disabled")
		set(ENABLE_HEIF OFF)
	endif()
endif()

# TIFF (optional)
if(ENABLE_TIFF)
	find_package(TIFF)
	if(TIFF_FOUND)
		add_definitions(-DENABLE_TIFF)
	else()
		message(WARNING "libtiff not found, TIFF support disabled")
		set(ENABLE_TIFF OFF)
	endif()
endif()

# RAW (optional)
if(ENABLE_RAW)
	pkg_check_modules(RAW libraw)
	if(RAW_FOUND)
		add_definitions(-DENABLE_RAW)
	else()
		message(WARNING "libraw not found, RAW support disabled")
		set(ENABLE_RAW OFF)
	endif()
endif()

# STB headers (vendored)
set(STB_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/vendor/stb)

# Add include directories
include_directories(
	${CMAKE_SOURCE_DIR}/src
	${STB_INCLUDE_DIR}
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

# Source files (PHASE 1-11 implemented)
set(IMGCAT2_SOURCES
	# Core module
	src/imgcat2/core/image.c
	src/imgcat2/core/pipeline.c
	src/imgcat2/core/cli.c

	# Decoders module
	src/imgcat2/decoders/decoder.c
	src/imgcat2/decoders/magic.c
	src/imgcat2/decoders/decoder_stb.c
	src/imgcat2/decoders/decoder_png.c
	src/imgcat2/decoders/decoder_jpeg.c

	# ANSI module
	src/imgcat2/ansi/ansi.c
	src/imgcat2/ansi/escape.c
)

if(GIF_FOUND)
	list(APPEND IMGCAT2_SOURCES src/imgcat2/decoders/decoder_gif.c)
endif()

# Platform-specific terminal module (PHASE 8)
if(WIN32)
	list(APPEND IMGCAT2_SOURCES src/imgcat2/terminal/terminal_windows.c)
else()
	list(APPEND IMGCAT2_SOURCES src/imgcat2/terminal/terminal_unix.c)
endif()

# Main executable
add_executable(imgcat2 ${IMGCAT2_SOURCES} src/imgcat2/main.c)

# Library for tests (without main.c)
add_library(imgcat2_lib STATIC ${IMGCAT2_SOURCES})

# Link libraries for executable
target_link_libraries(imgcat2 PRIVATE
	${PNG_LDFLAGS}
	${ZLIB_LIBRARIES}
	${JPEG_LIBRARIES}
)

# Link libraries for library
target_link_libraries(imgcat2_lib PUBLIC
	${PNG_LDFLAGS}
	${ZLIB_LIBRARIES}
	${JPEG_LIBRARIES}
)

if(GIF_FOUND)
	target_link_libraries(imgcat2 PUBLIC ${GIF_LIBRARY})
	target_link_libraries(imgcat2_lib PUBLIC ${GIF_LIBRARY})
endif()

if(WEBP_FOUND)
	target_link_libraries(imgcat2 PRIVATE ${WEBP_LDFLAGS})
	target_link_libraries(imgcat2_lib PUBLIC ${WEBP_LDFLAGS})
endif()

if(HEIF_FOUND)
	target_link_libraries(imgcat2 PRIVATE ${HEIF_LDFLAGS})
	target_link_libraries(imgcat2_lib PUBLIC ${HEIF_LDFLAGS})
endif()

if(TIFF_FOUND)
	target_link_libraries(imgcat2 PRIVATE ${TIFF_LIBRARIES})
	target_link_libraries(imgcat2_lib PUBLIC ${TIFF_LIBRARIES})
endif()

if(RAW_FOUND)
	target_link_libraries(imgcat2 PRIVATE ${RAW_LDFLAGS})
	target_link_libraries(imgcat2_lib PUBLIC ${RAW_LDFLAGS})
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
message(STATUS "  zlib: ${ZLIB_LIBRARIES}")
message(STATUS "  libjpeg: ${JPEG_LIBRARIES}")
message(STATUS "")
message(STATUS "Optional Format Support:")
message(STATUS "  GIF:  ${ENABLE_GIF}")
message(STATUS "  WebP: ${ENABLE_WEBP}")
message(STATUS "  HEIF: ${ENABLE_HEIF}")
message(STATUS "  TIFF: ${ENABLE_TIFF}")
message(STATUS "  RAW:  ${ENABLE_RAW}")
message(STATUS "")
message(STATUS "Other Options:")
message(STATUS "  Testing: ${BUILD_TESTING}")
message(STATUS "  clang-tidy: ${ENABLE_CLANG_TIDY}")
message(STATUS "  clang-format: ${ENABLE_CLANG_FORMAT}")
message(STATUS "")
