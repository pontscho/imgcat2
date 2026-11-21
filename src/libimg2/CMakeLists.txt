# libimg2: static image decoding / encoding / scaling library (target img2, alias img2::img2)

# Generated sources (embedded fonts) live in the build tree
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(IMG2_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(_img2_font_dir "${CMAKE_CURRENT_SOURCE_DIR}/font")
add_custom_command(
	OUTPUT "${IMG2_GENERATED_DIR}/embedded_fonts.c" "${IMG2_GENERATED_DIR}/embedded_fonts.h"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${IMG2_GENERATED_DIR}"
	COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/embed_font.py"
	        "${_img2_font_dir}" "${IMG2_GENERATED_DIR}/embedded_fonts"
	DEPENDS "${_img2_font_dir}/dejavu-sans-mono.book.ttf" "${_img2_font_dir}/dejavu-sans-mono.bold.ttf"
	        "${_img2_font_dir}/dejavu-sans-mono.oblique.ttf" "${_img2_font_dir}/dejavu-sans-mono.bold-oblique.ttf"
	        "${PROJECT_SOURCE_DIR}/tools/embed_font.py"
	COMMENT "Embedding DejaVu Sans Mono font family" VERBATIM)
add_custom_target(embed_fonts DEPENDS "${IMG2_GENERATED_DIR}/embedded_fonts.c" "${IMG2_GENERATED_DIR}/embedded_fonts.h")

add_library(img2 STATIC)
add_library(img2::img2 ALIAS img2)
# PIC (default ON): PIE executables and shared objects can link the static library
set_target_properties(img2 PROPERTIES POSITION_INDEPENDENT_CODE ${IMG2_POSITION_INDEPENDENT_CODE})
target_sources(img2 PRIVATE
	core/image.c core/log.c core/status.c core/init.c core/api.c
	decoders/decoder.c decoders/magic.c decoders/decoder_stb.c decoders/decoder_png.c decoders/decoder_jpeg.c
	decoders/decoder_qoi.c decoders/decoder_ico.c decoders/decoder_pnm.c decoders/decoder_yuv.c decoders/decoder_nanosvg.c
	encoders/encoder.c encoders/encoder_jpeg.c encoders/encoder_png.c
	text/font_manager.c "${IMG2_GENERATED_DIR}/embedded_fonts.c"
	metadata/xml_parser.c metadata/exif_reader.c)   # always compiled: exif/xmp are part of the image model

# Optional codecs
if(IMG2_HAVE_GIFLIB)
	target_sources(img2 PRIVATE decoders/decoder_gif.c)
endif()
if(IMG2_HAVE_WEBP)
	target_sources(img2 PRIVATE decoders/decoder_webp.c encoders/encoder_webp.c)
endif()
if(IMG2_HAVE_HEIF)
	target_sources(img2 PRIVATE decoders/decoder_heif.c decoders/decoder_avif.c encoders/encoder_heif.c)
endif()
if(IMG2_HAVE_TIFF)
	target_sources(img2 PRIVATE decoders/decoder_tiff.c)
endif()
if(IMG2_HAVE_RAW)
	target_sources(img2 PRIVATE decoders/decoder_raw.c)
endif()
if(IMG2_HAVE_JXL)
	target_sources(img2 PRIVATE decoders/decoder_jxl.c encoders/encoder_jxl.c)
endif()
if(IMG2_HAVE_RESVG)
	target_sources(img2 PRIVATE decoders/decoder_resvg.c)
endif()
add_dependencies(img2 embed_fonts)

# Public headers (include/img2) + generated img2_config.h (informative IMG2_HAS_* and the version)
foreach(_pair JPEG:LIBJPEG JPEGQS:JPEGQS PNG:LIBPNG GIF:GIFLIB WEBP:WEBP HEIF:HEIF TIFF:TIFF RAW:RAW JXL:JXL
		RESVG:RESVG EXIF:EXIF_READER)
	string(REPLACE ":" ";" _pair "${_pair}")
	list(GET _pair 0 _has)
	list(GET _pair 1 _have)
	if(IMG2_HAVE_${_have})
		set(IMG2_HAS_${_has} 1)
	else()
		set(IMG2_HAS_${_has} 0)
	endif()
endforeach()
if(IMG2_HAVE_LIBPNG AND PNG_APNG_SUPPORTED_CHECK)
	set(IMG2_HAS_APNG 1)
else()
	set(IMG2_HAS_APNG 0)
endif()
configure_file("${PROJECT_SOURCE_DIR}/cmake/img2_config.h.in" "${PROJECT_BINARY_DIR}/include/img2/img2_config.h")
target_include_directories(img2 PUBLIC
	"$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>" "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>"
	"$<INSTALL_INTERFACE:include>")

# [SL1] secure_getenv for the font manager's ~ expansion (only IMG2_HAVE_SECURE_GETENV is passed:
# font_manager.c defines _GNU_SOURCE itself)
include(CheckSymbolExists)
set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
check_symbol_exists(secure_getenv "stdlib.h" IMG2_HAVE_SECURE_GETENV)
unset(CMAKE_REQUIRED_DEFINITIONS)
if(IMG2_HAVE_SECURE_GETENV)
	set_source_files_properties(text/font_manager.c PROPERTIES COMPILE_DEFINITIONS IMG2_HAVE_SECURE_GETENV)
endif()

# Compile definitions and include directories (formerly directory-global definitions / include dirs).
# The nanosvg dir comes first, as on master (Img2VendorHeaderOnly.cmake used to add it globally before the
# main include list); then src/, the two S2 directories, stb, qoi, and the codec dirs in IMG2_INCLUDE_ORDER.
set(_img2_defs "")
set(_img2_incs ${NANOSVG_INCLUDE_DIR} ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR} ${IMG2_GENERATED_DIR}
	${STB_INCLUDE_DIR} ${QOI_INCLUDE_DIR})
foreach(_f IN LISTS IMG2_MACRO_FEATURES)          # no HAVE_ZLIB
	if(IMG2_HAVE_${_f})
		list(APPEND _img2_defs ${IMG2_FEATURE_MACRO_PREFIX}${_f})
	endif()
endforeach()
foreach(_f IN LISTS IMG2_INCLUDE_ORDER)           # jpegqs last
	if(IMG2_HAVE_${_f})
		list(APPEND _img2_incs ${IMG2_${_f}_INCLUDE_DIRS})
	endif()
endforeach()
target_compile_definitions(img2 PRIVATE ${_img2_defs})
target_include_directories(img2 PRIVATE ${_img2_incs})

# Link closure in the static link order of the former imgcat2 executable
foreach(_f IN LISTS IMG2_LINK_ORDER)
	if(IMG2_HAVE_${_f})
		target_link_libraries(img2 PRIVATE ${IMG2_${_f}_LIBS})
	endif()
endforeach()
target_link_libraries(img2 PRIVATE ${IMGCAT2_VENDORED_TRAILING_LIBS})
if(UNIX AND NOT APPLE)
	target_link_libraries(img2 PRIVATE m)
endif()
if(APPLE AND STATIC_LIBCXX_LIBS)
	target_link_libraries(img2 PRIVATE ${STATIC_LIBCXX_LIBS})
elseif(APPLE AND DYNAMIC_LIBCXX_LIBS)
	target_link_libraries(img2 PRIVATE ${DYNAMIC_LIBCXX_LIBS})
elseif(CMAKE_CXX_COMPILER_LOADED)
	# C++ vendors (LibRaw, libheif, libjxl, highway): an add_subdirectory consumer whose directory has only C
	# enabled links with the C driver, so the C++ runtime must be part of img2's link interface
	target_link_libraries(img2 PRIVATE stdc++)
endif()
target_link_libraries(img2 PRIVATE Threads::Threads)
img2_apply_project_settings(img2)

# [S6 item 11 / R13] -UNDEBUG object variant of img2's own sources for the no-terminate guard (img2_symbols):
# same sources, include dirs and definitions as img2, assert() live. Lives here because source-file
# properties (secure_getenv definition, generated embedded_fonts.c) are directory-scoped.
get_target_property(_img2_srcs img2 SOURCES)
set(IMG2_LIB_SOURCES "")
foreach(_s IN LISTS _img2_srcs)
	if(IS_ABSOLUTE "${_s}")
		list(APPEND IMG2_LIB_SOURCES "${_s}")
	else()
		list(APPEND IMG2_LIB_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${_s}")
	endif()
endforeach()
add_library(img2_assertcheck OBJECT EXCLUDE_FROM_ALL ${IMG2_LIB_SOURCES})
target_include_directories(img2_assertcheck PRIVATE $<TARGET_PROPERTY:img2,INCLUDE_DIRECTORIES>)
target_compile_definitions(img2_assertcheck PRIVATE $<TARGET_PROPERTY:img2,COMPILE_DEFINITIONS>)
target_compile_options(img2_assertcheck PRIVATE $<TARGET_PROPERTY:img2,COMPILE_OPTIONS> -UNDEBUG)
add_dependencies(img2_assertcheck embed_fonts)
# Build-time generated vendor headers (libjpeg-turbo install, pnglibconf.h, ...) come with img2's link
# dependencies; without this ordering a fresh parallel build may compile against the host's jpeglib.h.
add_dependencies(img2_assertcheck img2)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/img2_objects_debug.txt"
     CONTENT "$<JOIN:$<TARGET_OBJECTS:img2_assertcheck>,;>")
add_custom_target(img2_assertcheck_build ALL)
add_dependencies(img2_assertcheck_build img2_assertcheck)

# [SM4] Format-string hardening for img2's own sources only (not vendors, not the CLI)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
	target_compile_options(img2 PRIVATE -Wformat=2 -Wformat-security -Werror=format-security)
endif()

# Public header self-containment: one generated TU per public header (included twice), compiled strictly
# with ONLY include/ and the generated include dir ([Q8]); links nothing.
set(_img2_public_headers common log image mime yuv decode encode metadata font img2 img2_config)
set(_img2_hc_srcs "")
foreach(_h IN LISTS _img2_public_headers)
	set(_hc "${CMAKE_CURRENT_BINARY_DIR}/header_check/hc_${_h}.c")
	file(GENERATE OUTPUT "${_hc}" CONTENT
		"#include <img2/${_h}.h>\n#include <img2/${_h}.h>\nint img2_hc_${_h}(void);\nint img2_hc_${_h}(void) { return 0; }\n")
	list(APPEND _img2_hc_srcs "${_hc}")
endforeach()
add_library(img2_header_check OBJECT ${_img2_hc_srcs})
set_target_properties(img2_header_check PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
	target_compile_options(img2_header_check PRIVATE -std=c11 -Wall -Wextra -Werror -pedantic)
endif()
target_include_directories(img2_header_check PRIVATE "${PROJECT_SOURCE_DIR}/include" "${PROJECT_BINARY_DIR}/include")
add_dependencies(img2_header_check img2)

# In-tree users of the fat archive (CLI, tests): the merged libimg2.a (cmake/Img2FatArchive.cmake) with the same
# link interface as the installed package. add_subdirectory consumers keep the real img2 target (img2::img2).
if(IMG2_FAT_ARCHIVE)
	add_library(img2_bundled STATIC IMPORTED GLOBAL)
	set_target_properties(img2_bundled PROPERTIES
		IMPORTED_LOCATION "${IMG2_FAT_ARCHIVE_FILE}"
		IMPORTED_LINK_INTERFACE_LANGUAGES "${IMG2_FAT_LINK_LANGUAGES}"   # C;CXX with the C++ vendors (C++ driver)
		INTERFACE_INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/include;${PROJECT_BINARY_DIR}/include"
		INTERFACE_LINK_LIBRARIES "${IMG2_SYSTEM_LINK_LIBS}"
		INTERFACE_LINK_OPTIONS "${IMG2_INTERFACE_LINK_OPTIONS}")   # -fopenmp (Linux, if OpenMP); -no-pie if PIC OFF
	set(IMG2_LINK_TARGET img2_bundled)
else()
	set(IMG2_LINK_TARGET img2)
endif()
set(IMG2_LINK_TARGET "${IMG2_LINK_TARGET}" PARENT_SCOPE)

# White-box tests and the (transitional) CLI see the internal headers and feature macros.
# Never installed or exported.
add_library(img2_testing INTERFACE)
target_link_libraries(img2_testing INTERFACE ${IMG2_LINK_TARGET})
target_include_directories(img2_testing INTERFACE ${_img2_incs})
target_compile_definitions(img2_testing INTERFACE ${_img2_defs})
