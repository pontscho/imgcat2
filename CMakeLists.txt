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
# Warning flags (-Werror) are project-only: see IMGCAT2_WARNING_FLAGS, applied after
# the vendored dependencies are declared (COMPILE_OPTIONS is captured at target creation).
add_compile_options(-O3 -g)
set(IMGCAT2_WARNING_FLAGS -Wall -Wextra -Werror -Wno-error=unused-but-set-variable)

# pkg-config
find_package(PkgConfig REQUIRED)

include(Img2VendorHelpers)
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

include(Img2LinkOrder)
include(Img2ProjectSettings)

# Add include directories
include_directories(
	${PROJECT_SOURCE_DIR}/src
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
	include_directories(${PROJECT_SOURCE_DIR}/vendor/jpeg-quantsmooth)
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

if(IMG2_ENABLE_EXIF_READER)
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
	OUTPUT ${PROJECT_SOURCE_DIR}/src/imgcat2/font/embedded_fonts.c
	       ${PROJECT_SOURCE_DIR}/src/imgcat2/font/embedded_fonts.h
	COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/embed_font.py
	        ${PROJECT_SOURCE_DIR}/src/imgcat2/font
	        ${PROJECT_SOURCE_DIR}/src/imgcat2/font/embedded_fonts
	DEPENDS ${PROJECT_SOURCE_DIR}/src/imgcat2/font/dejavu-sans-mono.book.ttf
	        ${PROJECT_SOURCE_DIR}/src/imgcat2/font/dejavu-sans-mono.bold.ttf
	        ${PROJECT_SOURCE_DIR}/src/imgcat2/font/dejavu-sans-mono.oblique.ttf
	        ${PROJECT_SOURCE_DIR}/src/imgcat2/font/dejavu-sans-mono.bold-oblique.ttf
	        ${PROJECT_SOURCE_DIR}/tools/embed_font.py
	COMMENT "Embedding DejaVu Sans Mono font family (Regular, Bold, Italic, Bold-Italic)"
)

add_custom_target(embed_fonts ALL DEPENDS
	${PROJECT_SOURCE_DIR}/src/imgcat2/font/embedded_fonts.c
	${PROJECT_SOURCE_DIR}/src/imgcat2/font/embedded_fonts.h
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
	if(NOT IMG2_USE_SYSTEM_TIFF)
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
	if(NOT IMG2_USE_SYSTEM_RAW)
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
	if(NOT IMG2_USE_SYSTEM_JXL)
		# Vendored libjxl targets (carry highway/brotli/lcms2 as link dependencies)
		target_link_libraries(imgcat2 PRIVATE ${JXL_LIBRARIES})
		target_link_libraries(imgcat2_lib PUBLIC ${JXL_LIBRARIES})
	elseif(APPLE)
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
