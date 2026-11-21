# Test suite for img2 / imgcat2 (registered with img2_add_test(), cmake/Img2Testing.cmake)
#
# LINK lib (default): white-box library tests, linked against img2 via img2_testing.
# LINK cli: tests that also use CLI symbols (imgcat2_cli).
# REQUIRES <FEATURE>: registered only when the feature is built (img2_feature registry).

# Per-build temp dir: tests must never write to /tmp. Every test gets
# TMPDIR=<this dir> (img2_add_test), and the same path is compiled in as
# the fallback for running a test binary directly (IMGCAT2_TEST_TMPDIR).
set(IMGCAT2_TEST_TMPDIR "${PROJECT_BINARY_DIR}/test-tmp")
file(MAKE_DIRECTORY "${IMGCAT2_TEST_TMPDIR}")

# ============================================================================
# UNIT TESTS
# ============================================================================
img2_add_test(test_terminal SOURCES unit/test_terminal.c LINK cli)
img2_add_test(test_ansi SOURCES unit/test_ansi.c LINK cli)
img2_add_test(test_security SOURCES unit/test_security.c LINK cli)
img2_add_test(test_magic SOURCES unit/test_magic.c)
img2_add_test(test_cli_parsing SOURCES unit/test_cli_parsing.c LINK cli)
img2_add_test(test_decoder_png SOURCES unit/test_decoder_png.c WORKDIR ${PROJECT_BINARY_DIR})
img2_add_test(test_decoder_jpeg SOURCES unit/test_decoder_jpeg.c)
img2_add_test(test_decoder_stb SOURCES unit/test_decoder_stb.c)
img2_add_test(test_decoder_pnm SOURCES unit/test_decoder_pnm.c)
img2_add_test(test_decoder_yuv SOURCES unit/test_decoder_yuv.c LINK cli)
img2_add_test(test_image_processing SOURCES unit/test_image_processing.c)
img2_add_test(test_image_create_destroy SOURCES unit/test_image_create_destroy.c)
img2_add_test(test_image_orientation SOURCES unit/test_image_orientation.c)
img2_add_test(test_encoder SOURCES unit/test_encoder.c)
img2_add_test(test_encoder_jpeg SOURCES unit/test_encoder_jpeg.c)
img2_add_test(test_encoder_png SOURCES unit/test_encoder_png.c)
img2_add_test(test_encoder_webp SOURCES unit/test_encoder_webp.c REQUIRES WEBP)
img2_add_test(test_encoder_heif SOURCES unit/test_encoder_heif.c REQUIRES HEIF)
img2_add_test(test_encoder_jxl SOURCES unit/test_encoder_jxl.c REQUIRES JXL)
img2_add_test(test_file_render SOURCES unit/test_file_render.c LINK cli)
img2_add_test(test_xml_parser SOURCES unit/test_xml_parser.c REQUIRES EXIF_READER)
img2_add_test(test_exif_reader SOURCES unit/test_exif_reader.c REQUIRES EXIF_READER)
img2_add_test(test_metadata_output SOURCES unit/test_metadata_output.c LINK cli REQUIRES EXIF_READER)
img2_add_test(test_log SOURCES unit/test_log.c)

# ============================================================================
# INTEGRATION TESTS
# ============================================================================
img2_add_test(test_pipeline_end_to_end KIND integration SOURCES integration/test_pipeline_end_to_end.c LINK cli)
img2_add_test(test_cli_arguments_integration KIND integration SOURCES integration/test_cli_arguments.c LINK cli)
img2_add_test(test_animation KIND integration SOURCES integration/test_animation.c LINK cli)
img2_add_test(test_stdin_pipe KIND integration SOURCES integration/test_stdin_pipe.c LINK cli)
img2_add_test(test_cross_platform KIND integration SOURCES integration/test_cross_platform.c LINK cli)
img2_add_test(test_convert KIND integration SOURCES integration/test_convert.c LINK cli)

# EXIF integration tests (the optional decoder ones need that decoder in img2)
img2_add_test(test_jpeg_exif_integration KIND integration SOURCES integration/test_jpeg_exif_integration.c)
img2_add_test(test_png_exif_integration KIND integration SOURCES integration/test_png_exif_integration.c
	WORKDIR ${PROJECT_BINARY_DIR})
img2_add_test(test_webp_exif_integration KIND integration SOURCES integration/test_webp_exif_integration.c
	REQUIRES WEBP WORKDIR ${PROJECT_BINARY_DIR})
img2_add_test(test_heif_exif_integration KIND integration SOURCES integration/test_heif_exif_integration.c
	REQUIRES HEIF)
img2_add_test(test_jxl_exif_integration KIND integration SOURCES integration/test_jxl_exif_integration.c
	REQUIRES JXL)
img2_add_test(test_raw_exif_integration KIND integration SOURCES integration/test_raw_exif_integration.c
	LINK cli REQUIRES RAW TIMEOUT 30)
img2_add_test(test_tiff_exif_integration KIND integration SOURCES integration/test_tiff_exif_integration.c
	REQUIRES TIFF WORKDIR ${PROJECT_BINARY_DIR})

# Orientation integration tests
img2_add_test(test_jpeg_orientation KIND integration SOURCES integration/test_jpeg_orientation.c)
img2_add_test(test_png_orientation KIND integration SOURCES integration/test_png_orientation.c)
img2_add_test(test_webp_orientation KIND integration SOURCES integration/test_webp_orientation.c REQUIRES WEBP)
img2_add_test(test_heif_orientation KIND integration SOURCES integration/test_heif_orientation.c REQUIRES HEIF)
img2_add_test(test_avif_orientation KIND integration SOURCES integration/test_avif_orientation.c REQUIRES HEIF)
img2_add_test(test_jxl_orientation KIND integration SOURCES integration/test_jxl_orientation.c REQUIRES JXL)
img2_add_test(test_raw_orientation KIND integration SOURCES integration/test_raw_orientation.c
	REQUIRES RAW TIMEOUT 30)
img2_add_test(test_tiff_orientation KIND integration SOURCES integration/test_tiff_orientation.c REQUIRES TIFF)

# unit/test_xml_simple.c is an orphan and stays unregistered.

# ============================================================================
# GUARDS
# ============================================================================
add_test(NAME img2_layering_lib COMMAND ${CMAKE_COMMAND} -DMODE=lib -DROOT=${PROJECT_SOURCE_DIR}
	-P ${PROJECT_SOURCE_DIR}/cmake/scripts/img2_check_layering.cmake)
set_tests_properties(img2_layering_lib PROPERTIES LABELS guard)
