# Purpose: Sanitizer builds (ASan + UBSan, or TSan) of libimg2, the vendored codecs, the tests and imgcat2.
# Inputs:  IMG2_SANITIZE (cache string, cmake/Img2Options.cmake), IMG2_COVERAGE, CMAKE_C_COMPILER_ID,
#          IMG2_HARDENING_FLAGS (cmake/Img2Hardening.cmake).
# Outputs: IMG2_SANITIZE_FLAGS (compile flags), IMG2_SANITIZE_PROJECT_FLAGS (our own targets only, through
#          img2_apply_project_settings()), IMG2_HARDENING_FLAGS / IMG2_HARDENING_FLAGS_STRING (extended),
#          directory link option -fsanitize=<list> (every target created afterwards: vendors, img2, tests, CLI).
#
# The compile flags ride on IMG2_HARDENING_FLAGS, so they reach exactly the targets the hardening flags reach:
# our own targets (img2_apply_project_settings), the add_subdirectory() vendors (vendor section COMPILE_OPTIONS,
# C/C++ only) and the ExternalProject vendors (libjpeg-turbo, aom: CMAKE_C_FLAGS / CMAKE_CXX_FLAGS, which CMake
# also passes to their link steps). Deviations from the normal build, sanitizer configuration only:
#   * no -static / -static-pie (cmake/Img2Platform.cmake, src/imgcat2/CMakeLists.txt): the sanitizer runtimes
#     need the dynamic loader (ASan/TSan intercept libc through dlsym);
#   * _FORTIFY_SOURCE off (-U_FORTIFY_SOURCE): fortified calls go to glibc's __*_chk entry points, which the
#     sanitizer runtimes do not intercept, so ASan would not see those memcpy/strcpy/... accesses.
# Driver: scripts/sanitize.sh. See docs/sanitizers.md.

set(IMG2_SANITIZE_FLAGS "")
set(IMG2_SANITIZE_PROJECT_FLAGS "")
if(IMG2_SANITIZE)
	if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
		message(FATAL_ERROR "img2: IMG2_SANITIZE needs GCC or Clang; C compiler is ${CMAKE_C_COMPILER_ID}")
	endif()
	if(IMG2_COVERAGE)
		message(FATAL_ERROR "img2: IMG2_SANITIZE and IMG2_COVERAGE are separate build configurations")
	endif()
	string(REPLACE "," ";" _img2_san_list "${IMG2_SANITIZE}")
	foreach(_s IN LISTS _img2_san_list)
		if(NOT _s MATCHES "^(address|undefined|thread)$")
			message(FATAL_ERROR "img2: IMG2_SANITIZE: unsupported sanitizer '${_s}' "
				"(supported: address,undefined or thread)")
		endif()
	endforeach()
	if("thread" IN_LIST _img2_san_list AND "address" IN_LIST _img2_san_list)
		message(FATAL_ERROR "img2: IMG2_SANITIZE: thread and address cannot be combined")
	endif()
	string(JOIN "," _img2_san ${_img2_san_list})
	set(IMG2_SANITIZE_FLAGS -fsanitize=${_img2_san} -fno-omit-frame-pointer -U_FORTIFY_SOURCE)
	if("undefined" IN_LIST _img2_san_list)
		# Our own targets: every UBSan finding is fatal and cannot be suppressed. The vendors keep clang's
		# recoverable UBSan handlers, so a documented vendor finding can be listed in the suppressions file
		# (scripts/sanitizers/ubsan.supp); with UBSAN_OPTIONS=halt_on_error=1 every other one still stops the run.
		set(IMG2_SANITIZE_PROJECT_FLAGS -fno-sanitize-recover=undefined)
	endif()
	list(REMOVE_ITEM IMG2_HARDENING_FLAGS -D_FORTIFY_SOURCE=2)
	list(APPEND IMG2_HARDENING_FLAGS ${IMG2_SANITIZE_FLAGS})
	string(JOIN " " IMG2_HARDENING_FLAGS_STRING ${IMG2_HARDENING_FLAGS})
	add_link_options(-fsanitize=${_img2_san})
	message(STATUS "img2: sanitizers ON (${_img2_san}): libimg2, vendored codecs, tests, imgcat2; dynamic link")
endif()
