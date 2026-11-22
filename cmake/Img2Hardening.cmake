# Purpose: Compiler hardening flags shared by our own targets and the vendored codecs ([SL2], F32).
# Inputs:  CMAKE_C_COMPILER_ID.
# Outputs: IMG2_HARDENING_FLAGS (list), IMG2_HARDENING_FLAGS_STRING (space-joined, for ExternalProject
#          CMAKE_<LANG>_FLAGS), IMG2_FORTIFY_PREDEFINED (check result).
# Included before img2_vendor_section_begin(): the vendor section adds the flags (C/C++ only) to the vendored
# targets; img2_apply_project_settings() adds them to ours. Hardening flags only, never warning flags.

# _FORTIFY_SOURCE is added only when the toolchain does not predefine it at our optimisation level (Ubuntu's
# GCC does; redefining it would trip -Werror on our targets and warn on the vendors). Our targets and the
# vendor targets are both built with -O3, so one probe serves both.
set(IMG2_HARDENING_FLAGS "")
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
	include(CheckCSourceCompiles)
	include(CMakePushCheckState)
	cmake_push_check_state(RESET)
	set(CMAKE_REQUIRED_FLAGS -O3)
	check_c_source_compiles("#ifndef _FORTIFY_SOURCE\n#error not predefined\n#endif\nint main(void){return 0;}"
		IMG2_FORTIFY_PREDEFINED)
	cmake_pop_check_state()
	list(APPEND IMG2_HARDENING_FLAGS -fstack-protector-strong)
	if(NOT IMG2_FORTIFY_PREDEFINED)
		list(APPEND IMG2_HARDENING_FLAGS -D_FORTIFY_SOURCE=2)   # -O3 is always on for our and the vendor targets
	endif()
endif()
string(JOIN " " IMG2_HARDENING_FLAGS_STRING ${IMG2_HARDENING_FLAGS})
