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
target_sources(img2 PRIVATE
	core/image.c
	decoders/decoder.c decoders/magic.c decoders/decoder_stb.c decoders/decoder_png.c decoders/decoder_jpeg.c
	decoders/decoder_qoi.c decoders/decoder_ico.c decoders/decoder_pnm.c decoders/decoder_yuv.c decoders/decoder_nanosvg.c
	encoders/encoder.c encoders/encoder_jpeg.c encoders/encoder_png.c
	text/font_manager.c "${IMG2_GENERATED_DIR}/embedded_fonts.c")

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
if(IMG2_HAVE_EXIF_READER)
	target_sources(img2 PRIVATE metadata/xml_parser.c metadata/exif_reader.c)
endif()
add_dependencies(img2 embed_fonts)

# Compile definitions and include directories (the former global add_definitions / include_directories).
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
endif()
img2_apply_project_settings(img2)

# White-box tests and the (transitional) CLI see the internal headers and feature macros.
# Never installed or exported.
add_library(img2_testing INTERFACE)
target_link_libraries(img2_testing INTERFACE img2)
target_include_directories(img2_testing INTERFACE ${_img2_incs})
target_compile_definitions(img2_testing INTERFACE ${_img2_defs})
