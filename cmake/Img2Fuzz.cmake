# Purpose: libFuzzer + ASan/UBSan build of libimg2 and the fuzz targets in src/tests/fuzz (IMG2_FUZZ).
# Inputs:  IMG2_FUZZ (option, default OFF), IMG2_COVERAGE, IMG2_SANITIZE, CMAKE_C_COMPILER_ID, IMG2_HARDENING_FLAGS and
#          IMG2_EXE_LINK_FLAGS (cmake/Img2Hardening.cmake, cmake/Img2Platform.cmake).
# Outputs: IMG2_FUZZ_SANITIZE_FLAGS (compile flags of every sanitized target), IMG2_FUZZ_LINK_FLAGS (fuzz target
#          link flags), function img2_fuzz_instrument(<tgt>); with IMG2_FUZZ ON it REPLACES IMG2_HARDENING_FLAGS
#          (+ _STRING) by the sanitizer flags and empties IMG2_EXE_LINK_FLAGS.
#
# Included after Img2Hardening and before img2_vendor_section_begin(), so the vendored codecs pick the sanitizer
# flags up through the existing hardening plumbing: the directory COMPILE_OPTIONS of the vendor section
# (add_subdirectory vendors, C/C++ only) and CMAKE_C_FLAGS/CMAKE_CXX_FLAGS of the ExternalProject vendors
# (libjpeg-turbo, aom). vendor/ itself is not touched. Vendors get -fsanitize=fuzzer-no-link,address,undefined
# with UBSan in recover mode (vendor UB is reported and the run continues); libimg2's own sources additionally get
# -fno-sanitize-recover=undefined so UB in our code aborts like an ASan finding.
# Fuzz-config-only changes to the hardening defaults:
#   - -D_FORTIFY_SOURCE is dropped (the fortified libc wrappers bypass the ASan interceptors);
#   - -fstack-protector-strong is dropped (ASan's stack redzones cover it);
#   - -static / -static-pie are dropped (IMG2_EXE_LINK_FLAGS emptied: the sanitizer runtimes need a dynamic link;
#     the fuzz targets are ordinary dynamic PIE executables and never call img2_static_executable()).
# Driver: scripts/fuzz.sh (build-fuzz/). See docs/fuzzing.md.

option(IMG2_FUZZ "Build the libFuzzer targets (src/tests/fuzz) with ASan/UBSan; libimg2 and the vendors sanitized (Clang only)" OFF)

set(IMG2_FUZZ_SANITIZE_FLAGS "")
set(IMG2_FUZZ_LINK_FLAGS "")
if(IMG2_FUZZ)
	if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
		message(FATAL_ERROR "img2: IMG2_FUZZ=ON requires Clang (libFuzzer, -fsanitize=fuzzer); C compiler is "
			"${CMAKE_C_COMPILER_ID}. Configure with -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ "
			"(scripts/fuzz.sh does).")
	endif()
	if(IMG2_COVERAGE)
		message(FATAL_ERROR "img2: IMG2_FUZZ and IMG2_COVERAGE are mutually exclusive (use separate build trees)")
	endif()
	if(IMG2_SANITIZE)
		message(FATAL_ERROR "img2: IMG2_FUZZ and IMG2_SANITIZE are mutually exclusive (IMG2_FUZZ already builds "
			"with ASan/UBSan; use separate build trees)")
	endif()
	set(IMG2_FUZZ_SANITIZE_FLAGS -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
	set(IMG2_FUZZ_LINK_FLAGS -fsanitize=fuzzer,address,undefined)
	set(IMG2_HARDENING_FLAGS ${IMG2_FUZZ_SANITIZE_FLAGS})
	string(JOIN " " IMG2_HARDENING_FLAGS_STRING ${IMG2_HARDENING_FLAGS})
	set(IMG2_EXE_LINK_FLAGS "")
	message(STATUS "img2: fuzzing build ON (libFuzzer + ASan/UBSan; libimg2 and the vendored codecs instrumented)")
endif()

# libimg2's own sources: UBSan findings abort (vendors stay in recover mode), except the benign idioms of the
# header-only vendored code compiled into libimg2's translation units listed in src/tests/fuzz/ubsan_ignorelist.txt;
# every in-tree executable that links <tgt> gets the sanitizer runtimes (BUILD_INTERFACE only, never exported).
function(img2_fuzz_instrument tgt)
	if(IMG2_FUZZ)
		target_compile_options(${tgt} PRIVATE -fno-sanitize-recover=undefined
			"-fsanitize-ignorelist=${PROJECT_SOURCE_DIR}/src/tests/fuzz/ubsan_ignorelist.txt")
		# -fsanitize-link-c++-runtime: C-linked consumers (the unit tests) still get UBSan's C++ part, which the
		# sanitized C++ vendors reference
		target_link_options(${tgt} INTERFACE "$<BUILD_INTERFACE:-fsanitize=address,undefined;-fsanitize-link-c++-runtime>")
	endif()
endfunction()
