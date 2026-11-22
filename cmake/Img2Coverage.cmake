# Purpose: Source-based code coverage of libimg2's own sources (clang -fprofile-instr-generate -fcoverage-mapping).
# Inputs:  IMG2_COVERAGE (option, default OFF), CMAKE_C_COMPILER_ID.
# Outputs: IMG2_COVERAGE_FLAGS (compile + link flags), function img2_coverage_instrument(<tgt>).
#
# Only img2's own sources are instrumented (src/libimg2/CMakeLists.txt calls img2_coverage_instrument(img2));
# the vendored codecs and the imgcat2 CLI sources are compiled as usual. Everything that links img2 (tests, the
# CLI) receives the link flag through img2's / img2_bundled's in-tree link interface, which pulls in the clang
# profile runtime. The installed package (IMG2_CONFIG_LINK_OPTIONS, exported targets) never sees the flags.
# Driver: scripts/coverage.sh (build-cov/, llvm-profdata + llvm-cov report/show). See docs/coverage.md.

option(IMG2_COVERAGE "Instrument libimg2's own sources for clang source-based coverage (llvm-cov)" OFF)

set(IMG2_COVERAGE_FLAGS "")
if(IMG2_COVERAGE)
	if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
		message(FATAL_ERROR "img2: IMG2_COVERAGE=ON requires Clang (source-based coverage, "
			"-fprofile-instr-generate -fcoverage-mapping); C compiler is ${CMAKE_C_COMPILER_ID}. "
			"Configure with -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ (scripts/coverage.sh does).")
	endif()
	# -fprofile-update=atomic: test_threads decodes on several threads at once; with plain increments the
	# counters lose updates, and outcomes clang derives by subtraction (branch / region counts) then read
	# spuriously non-zero or zero, which makes scripts/coverage_check.py flaky.
	set(IMG2_COVERAGE_FLAGS -fprofile-instr-generate -fcoverage-mapping -fprofile-update=atomic)
	message(STATUS "img2: coverage instrumentation ON (libimg2 sources only)")
endif()

# Instrument <tgt>'s own sources and put the runtime link flag on its in-tree (BUILD_INTERFACE) link interface.
function(img2_coverage_instrument tgt)
	if(IMG2_COVERAGE)
		target_compile_options(${tgt} PRIVATE ${IMG2_COVERAGE_FLAGS})
		target_link_options(${tgt} INTERFACE "$<BUILD_INTERFACE:-fprofile-instr-generate>")
	endif()
endfunction()
