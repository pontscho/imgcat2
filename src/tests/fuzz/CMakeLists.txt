# libFuzzer targets (IMG2_FUZZ only; cmake/Img2Fuzz.cmake, scripts/fuzz.sh, docs/fuzzing.md).
# libimg2 and the vendored codecs are compiled with -fsanitize=fuzzer-no-link,address,undefined; the targets link
# the libFuzzer runtime (-fsanitize=fuzzer) with the C++ driver (vendored LibRaw / libheif / libjxl are C++, and
# their UBSan checks need the C++ part of the UBSan runtime). Built only on request (EXCLUDE_FROM_ALL is not used:
# a fuzz build tree exists for nothing else).

set(_img2_fuzz_targets fuzz_decode fuzz_decode_forced fuzz_exif fuzz_xmp_xml fuzz_yuv)

foreach(_t IN LISTS _img2_fuzz_targets)
	add_executable(${_t} ${_t}.c)
	target_link_libraries(${_t} PRIVATE img2)
	target_include_directories(${_t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/libimg2)
	img2_apply_project_settings(${_t})
	target_compile_options(${_t} PRIVATE -fno-sanitize-recover=undefined)
	target_link_options(${_t} PRIVATE ${IMG2_FUZZ_LINK_FLAGS})
	if(CMAKE_CXX_COMPILER_LOADED)
		set_target_properties(${_t} PROPERTIES LINKER_LANGUAGE CXX)
	endif()
endforeach()

# Seed corpus generator: an ordinary (sanitized) program, no libFuzzer main
add_executable(fuzz_seedgen fuzz_seedgen.c)
target_link_libraries(fuzz_seedgen PRIVATE img2)
target_include_directories(fuzz_seedgen PRIVATE ${STB_INCLUDE_DIR} ${QOI_INCLUDE_DIR})
img2_apply_project_settings(fuzz_seedgen)
target_link_options(fuzz_seedgen PRIVATE -fsanitize=address,undefined)
if(CMAKE_CXX_COMPILER_LOADED)
	set_target_properties(fuzz_seedgen PROPERTIES LINKER_LANGUAGE CXX)
endif()
