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
img2_add_test(test_threads SOURCES unit/test_threads.c TIMEOUT 120)
if(TARGET test_threads)
	target_link_libraries(test_threads PRIVATE Threads::Threads)
	target_compile_definitions(test_threads PRIVATE IMG2_TEST_SRC_DIR="${PROJECT_SOURCE_DIR}/src/tests")
endif()
# S11: delay_ms / loop_count of the anim/* fixtures (public API only)
img2_add_test(test_animation_timing SOURCES unit/test_animation_timing.c LINK lib)
if(TARGET test_animation_timing)
	target_compile_definitions(test_animation_timing PRIVATE IMG2_TEST_SRC_DIR="${PROJECT_SOURCE_DIR}/src/tests")
endif()
# S13b: animated JXL / WebP / multi-image HEIF+AVIF, fixtures encoded in-test with the vendored encoders
img2_add_test(test_animation_jxl SOURCES unit/test_animation_jxl.c LINK lib REQUIRES JXL)
img2_add_test(test_animation_webp SOURCES unit/test_animation_webp.c LINK lib REQUIRES WEBP)
img2_add_test(test_animation_heif SOURCES unit/test_animation_heif.c LINK lib REQUIRES HEIF)
# Coverage (docs/coverage.md): core edge cases, ICO/QOI/PNM/YUV edge cases
img2_add_test(test_core_edges SOURCES unit/test_core_edges.c)
img2_add_test(test_decoder_edges SOURCES unit/test_decoder_edges.c)
# Allocation-failure injection through GNU ld --wrap (unit/alloc_fail.h): test executable only, Linux only
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	img2_add_test(test_alloc_fail SOURCES unit/test_alloc_fail.c TIMEOUT 120)
	if(TARGET test_alloc_fail)
		target_link_options(test_alloc_fail PRIVATE
			-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=strdup)
		target_compile_definitions(test_alloc_fail PRIVATE IMG2_TEST_SRC_DIR="${PROJECT_SOURCE_DIR}/src/tests")
	endif()
endif()

# [L4] Public API test: links img2 only (the fat archive if built; no img2_testing, no internal include dirs);
# includes only <img2/img2.h>
add_executable(test_public_api unit/main.c unit/test_public_api.c)
target_link_libraries(test_public_api PRIVATE ${IMG2_LINK_TARGET})
if(TARGET img2_fat)
	add_dependencies(test_public_api img2_fat)
endif()
target_compile_definitions(test_public_api PRIVATE IMG2_TEST_SRC_DIR="${PROJECT_SOURCE_DIR}/src/tests")
img2_apply_project_settings(test_public_api)   # dynamic (PIE) link, like every img2_add_test() test
add_test(NAME test_public_api COMMAND test_public_api)
set(_img2_public_api_timeout 10)
if(IMG2_SANITIZE)   # same factor as img2_add_test()
	set(_img2_public_api_timeout 100)
endif()
set_tests_properties(test_public_api PROPERTIES TIMEOUT ${_img2_public_api_timeout} LABELS "unit;lib"
	ENVIRONMENT "TMPDIR=${PROJECT_BINARY_DIR}/test-tmp")

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
add_test(NAME img2_layering_cli COMMAND ${CMAKE_COMMAND} -DMODE=cli -DROOT=${PROJECT_SOURCE_DIR}
	-P ${PROJECT_SOURCE_DIR}/cmake/scripts/img2_check_layering.cmake)
set_tests_properties(img2_layering_cli PROPERTIES LABELS guard)
add_test(NAME img2_public_headers COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR}
	-P ${PROJECT_SOURCE_DIR}/cmake/scripts/img2_check_public_headers.cmake)
set_tests_properties(img2_public_headers PROPERTIES LABELS guard)
if(CMAKE_NM AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
	# prefix + no-terminate (forbidden references, also in the -UNDEBUG img2_assertcheck objects)
	# + [SM3] mutable-state allow-list
	add_test(NAME img2_symbols COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} -DPREFIX=img2_
		-DARCHIVE=$<TARGET_FILE:img2>
		-DOBJ_LIST_FILE=${PROJECT_BINARY_DIR}/src/libimg2/img2_objects_debug.txt
		"-DFORBIDDEN=exit\;_exit\;_Exit\;quick_exit\;abort\;__assert_fail\;__assert_perror_fail\;__assert"
		-DOBJDUMP=${CMAKE_OBJDUMP}
		-DMUTABLE_ALLOWLIST=${PROJECT_SOURCE_DIR}/cmake/scripts/img2_mutable_allowlist.txt
		-DSRC_DIR=${PROJECT_SOURCE_DIR}/src/libimg2
		-P ${PROJECT_SOURCE_DIR}/cmake/scripts/img2_check_symbols.cmake)
	set_tests_properties(img2_symbols PROPERTIES LABELS guard)
endif()

# ============================================================================
# CONSUMER TESTS (standalone project src/tests/consumer; top-level builds only)
# ============================================================================
if(PROJECT_IS_TOP_LEVEL)
	# [L4] TMPDIR captured at configure time (gate.sh exports it); never hard-coded.
	set(_img2_consumer_tmp "$ENV{TMPDIR}")
	if(_img2_consumer_tmp STREQUAL "")
		set(_img2_consumer_tmp "${PROJECT_BINARY_DIR}/test-tmp")
	endif()
	set(_img2_consumer_args -DSRC=${PROJECT_SOURCE_DIR} -DBIN=${PROJECT_BINARY_DIR} "-DGENERATOR=${CMAKE_GENERATOR}"
		-DIMG=${PROJECT_SOURCE_DIR}/src/tests/data/png/rgb8_srgb.png
		-P ${PROJECT_SOURCE_DIR}/cmake/scripts/img2_consumer_smoke.cmake)
	if(IMG2_INSTALL)
		# install --component img2_development into the build tree, move the prefix, check for path leaks,
		# then find_package(img2) from a fresh consumer
		add_test(NAME img2_consumer_find_package COMMAND ${CMAKE_COMMAND} -DMODE=package ${_img2_consumer_args})
		set_tests_properties(img2_consumer_find_package PROPERTIES LABELS consumer TIMEOUT 900
			RESOURCE_LOCK img2_consumer PASS_REGULAR_EXPRESSION "img2 consumer OK"
			ENVIRONMENT "TMPDIR=${_img2_consumer_tmp}")
	endif()
	# add_subdirectory consumer: rebuilds every vendor (opt-in: label expensive) + cache leak audit
	add_test(NAME img2_consumer_subdir COMMAND ${CMAKE_COMMAND} -DMODE=subdir ${_img2_consumer_args})
	set_tests_properties(img2_consumer_subdir PROPERTIES LABELS "consumer;expensive" TIMEOUT 7200
		RESOURCE_LOCK img2_consumer PASS_REGULAR_EXPRESSION "img2 consumer OK"
		ENVIRONMENT "TMPDIR=${_img2_consumer_tmp}")
endif()

# --- coverage lane A begin ---
# Coverage phase 2, lane A (core, ICO/QOI/PNM/YUV/stb decoders, XML/EXIF, font manager); fixtures built in-test
img2_add_test(test_cov_a_ico SOURCES unit/test_cov_a_ico.c)
img2_add_test(test_cov_a_xml SOURCES unit/test_cov_a_xml.c REQUIRES EXIF_READER)
if(TARGET test_cov_a_xml)
	target_link_libraries(test_cov_a_xml PRIVATE Threads::Threads)
endif()
img2_add_test(test_cov_a_font SOURCES unit/test_cov_a_font.c)
img2_add_test(test_cov_a_exif SOURCES unit/test_cov_a_exif.c REQUIRES EXIF_READER)
img2_add_test(test_cov_a_yuv SOURCES unit/test_cov_a_yuv.c)
img2_add_test(test_cov_a_core SOURCES unit/test_cov_a_core.c)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	img2_add_test(test_cov_a_alloc SOURCES unit/test_cov_a_alloc.c REQUIRES EXIF_READER TIMEOUT 60)
	if(TARGET test_cov_a_alloc)
		target_link_options(test_cov_a_alloc PRIVATE
			-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=strdup)
	endif()
	img2_add_test(test_cov_a_nostrdup SOURCES unit/test_cov_a_nostrdup.c)
	if(TARGET test_cov_a_nostrdup)
		target_link_options(test_cov_a_nostrdup PRIVATE -Wl,--wrap=strdup)
	endif()
endif()
# --- coverage lane A end ---

# --- coverage lane B begin ---
# Coverage (docs/coverage.md) of the GIF, PNG, WebP, JPEG, TIFF, RAW and nanosvg decoders; fixtures built in-test
img2_add_test(test_cov_b_gif SOURCES unit/test_cov_b_gif.c REQUIRES GIFLIB)
img2_add_test(test_cov_b_png SOURCES unit/test_cov_b_png.c)
img2_add_test(test_cov_b_webp SOURCES unit/test_cov_b_webp.c REQUIRES WEBP)
img2_add_test(test_cov_b_jpeg SOURCES unit/test_cov_b_jpeg.c)
if(TEST test_cov_b_jpeg)
	# Hundreds of tiny decodes each start an OpenMP team (jpeg-quantsmooth); spinning idle threads
	# oversubscribe the host under ctest -j and push the test past its timeout
	set_property(TEST test_cov_b_jpeg APPEND PROPERTY ENVIRONMENT "OMP_NUM_THREADS=2")
endif()
img2_add_test(test_cov_b_tiff SOURCES unit/test_cov_b_tiff.c REQUIRES TIFF)
img2_add_test(test_cov_b_svg SOURCES unit/test_cov_b_svg.c)
img2_add_test(test_cov_b_raw SOURCES unit/test_cov_b_raw.c REQUIRES RAW TIMEOUT 30)
if(TARGET test_cov_b_raw)
	target_compile_definitions(test_cov_b_raw PRIVATE IMG2_TEST_SRC_DIR="${PROJECT_SOURCE_DIR}/src/tests")
endif()
# Allocation-failure injection (unit/alloc_fail.h), linked like test_alloc_fail: Linux only
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	img2_add_test(test_cov_b_alloc SOURCES unit/test_cov_b_alloc.c TIMEOUT 120)
	if(TARGET test_cov_b_alloc)
		target_link_options(test_cov_b_alloc PRIVATE
			-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=strdup)
	endif()
endif()
# --- coverage lane B end ---

# --- coverage lane C begin ---
# Coverage phase 2, lane C (docs/coverage.md): HEIF/AVIF/JXL decoders, every encoder
img2_add_test(test_cov_c_heif SOURCES unit/test_cov_c_heif.c REQUIRES HEIF EXIF_READER TIMEOUT 60)
img2_add_test(test_cov_c_jxl SOURCES unit/test_cov_c_jxl.c REQUIRES JXL EXIF_READER TIMEOUT 60)
img2_add_test(test_cov_c_encoders SOURCES unit/test_cov_c_encoders.c REQUIRES WEBP)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	img2_add_test(test_cov_c_alloc SOURCES unit/test_cov_c_alloc.c REQUIRES HEIF JXL LIBPNG LIBJPEG TIMEOUT 120)
	if(TARGET test_cov_c_alloc)
		target_link_options(test_cov_c_alloc PRIVATE
			-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=strdup)
		# Label no-sanitize (scripts/sanitize.sh skips it, docs/sanitizers.md): it forks children of a process
		# that has run kvazaar's worker threads, and they are expected to crash inside kvazaar after injected
		# allocation failures. Under ASan every such crash is a report and a child symbolising it blocks on the
		# parent's symbolizer pipe; under TSan the children deadlock (fork of a multi-threaded process); its leak
		# checks read glibc mallinfo2(), which the sanitizer allocators bypass. It runs in the normal build.
		set_property(TEST test_cov_c_alloc APPEND PROPERTY LABELS no-sanitize)
	endif()
endif()
# --- coverage lane C end ---

# --- coverage phase 3 begin ---
# Coverage phase 3 (docs/coverage.md): leak checks with a live-block counter, remaining gaps, bug-fix tests
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	img2_add_test(test_cov_p3_leak SOURCES unit/test_cov_p3_leak.c REQUIRES EXIF_READER)
	if(TARGET test_cov_p3_leak)
		target_link_options(test_cov_p3_leak PRIVATE
			-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=strdup,--wrap=free)
	endif()
	# guard-page allocations for font buffers (reads past a font file fault deterministically)
	img2_add_test(test_cov_p3_font SOURCES unit/test_cov_p3_font.c)
	if(TARGET test_cov_p3_font)
		target_link_options(test_cov_p3_font PRIVATE -Wl,--wrap=malloc,--wrap=free)
	endif()
endif()
img2_add_test(test_cov_p3_jxl SOURCES unit/test_cov_p3_jxl.c REQUIRES JXL EXIF_READER)
img2_add_test(test_cov_p3_gaps SOURCES unit/test_cov_p3_gaps.c REQUIRES GIFLIB)
# --- coverage phase 3 end ---

# Regression tests of the sanitizer sweep (docs/sanitizers.md)
img2_add_test(test_sanitize_regress SOURCES unit/test_sanitize_regress.c REQUIRES RAW EXIF_READER TIMEOUT 30)

# --- heif-fix begin ---
# HEIF/AVIF exclusion audit: byte-patched edge cases, and fake libheif codec plugins (the plugin registry is
# process-global, so each plugin test has its own executable)
img2_add_test(test_heif_fix_edges SOURCES unit/test_heif_fix_edges.c REQUIRES HEIF)
img2_add_test(test_heif_fix_decplugin SOURCES unit/test_heif_fix_decplugin.c REQUIRES HEIF TIMEOUT 60)
img2_add_test(test_heif_fix_encplugin SOURCES unit/test_heif_fix_encplugin.c REQUIRES HEIF)
# --- heif-fix end ---

# --- core-fix begin ---
# Exclusions the audit found reachable (docs/coverage-exclusions.txt): stb_image zero dimensions, RAW make /
# 2-colour preview / libraw_init failure, TIFF multipage with no directories (two allocation faults)
img2_add_test(test_core_fix_raw SOURCES unit/test_core_fix_raw.c REQUIRES RAW TIMEOUT 30)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	img2_add_test(test_core_fix_alloc SOURCES unit/test_core_fix_alloc.c TIMEOUT 60)
	if(TARGET test_core_fix_alloc)
		target_link_options(test_core_fix_alloc PRIVATE
			-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=strdup,--wrap=free)
	endif()
	# operator new throwing std::bad_alloc (unit/core_fix_new_wrap.cpp)
	img2_add_test(test_core_fix_new SOURCES unit/test_core_fix_new.c unit/core_fix_new_wrap.cpp REQUIRES RAW
		TIMEOUT 30)
	if(TARGET test_core_fix_new)
		target_link_options(test_core_fix_new PRIVATE -Wl,--wrap=_Znwm,--wrap=_Znam)
	endif()
endif()
# --- core-fix end ---

# --- fuzz regressions begin ---
# Reproducers of the libFuzzer findings (docs/fuzzing.md, src/tests/fuzz), embedded in the test; per-format cases are
# compiled in by the IMG2_HAVE_* macros of img2_testing
img2_add_test(test_fuzz_regress SOURCES unit/test_fuzz_regress.c TIMEOUT 30)
# --- fuzz regressions end ---

# --- iso-widen ---
# ISO above 65535: ISOSpeedRatings 65535 resolved through the EXIF 2.3 LONG sensitivity tags (metadata/exif_reader.c)
img2_add_test(test_iso_widen SOURCES unit/test_iso_widen.c REQUIRES EXIF_READER)
