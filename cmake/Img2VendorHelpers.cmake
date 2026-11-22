# Purpose: Helpers for vendored subprojects (test/install isolation, host ldflag stripping).
# Inputs:  BUILD_TESTING, IMG2_EXE_LINK_FLAGS, IMG2_POSITION_INDEPENDENT_CODE, IMG2_HARDENING_FLAGS, CMAKE_BUILD_TYPE /
#          CMAKE_CONFIGURATION_TYPES, vendored targets (ZLIB::ZLIB, png_static, webp, ...) at call time.
# Outputs: macros imgcat2_vendor_begin/imgcat2_vendor_end, img2_vendor_section_begin/end,
#          function imgcat2_strip_vendored_ldflags().

# Helper for vendored add_subdirectory() projects: their own tests/tools must never
# register with our CTest or build by default.
# imgcat2_vendor_begin(NO_INSTALL) additionally skips the install rules of the
# subproject (directory-scoped CMAKE_SKIP_INSTALL_RULES): upstream install(EXPORT)
# sets would otherwise require every vendored dependency they link (zlib-ng, brotli,
# kvazaar, ...) to be in an export set too. imgcat2's own install rules are unaffected.
macro(imgcat2_vendor_begin)
	set(_IMGCAT2_SAVED_BUILD_TESTING ${BUILD_TESTING})
	set(BUILD_TESTING OFF)
	# Macro arguments are not variables: copy ARGN before IN_LIST
	set(_imgcat2_vendor_args ${ARGN})
	if("NO_INSTALL" IN_LIST _imgcat2_vendor_args)
		set(CMAKE_SKIP_INSTALL_RULES ON)
	endif()
endmacro()
macro(imgcat2_vendor_end)
	set(BUILD_TESTING ${_IMGCAT2_SAVED_BUILD_TESTING})
	unset(CMAKE_SKIP_INSTALL_RULES)
endmacro()

# Host static link flags (pkg-config --static) of libraries that still come from the
# host may name dependencies that are vendored here (-ljpeg, -lwebp, -lz, ...). Linking
# the host copies next to the vendored ones gives duplicate symbols / mixed ABIs, so
# they are dropped; the vendored providers are linked last instead
# (IMGCAT2_VENDORED_TRAILING_LIBS, after all host libraries).
function(imgcat2_strip_vendored_ldflags _var)
	set(_out "")
	foreach(_f IN LISTS ${_var})
		if((TARGET ZLIB::ZLIB AND _f STREQUAL "-lz")
			OR (TARGET libjpeg-turbo-ext AND _f MATCHES "^-l(jpeg|turbojpeg)$")
			OR (TARGET png_static AND _f MATCHES "^-lpng(16)?$")
			OR (TARGET vendor_giflib AND _f STREQUAL "-lgif")
			OR (TARGET webp AND _f MATCHES "^-l(webp|webpdemux|webpmux|sharpyuv)$")
			OR (TARGET vendor_lcms2 AND _f STREQUAL "-llcms2")
			OR (TARGET brotlicommon AND _f MATCHES "^-lbrotli(common|dec|enc)$")
			OR (TARGET hwy AND _f STREQUAL "-lhwy"))
			continue()
		endif()
		list(APPEND _out "${_f}")
	endforeach()
	set(${_var} "${_out}" PARENT_SCOPE)
endfunction()

# [R1] NDEBUG only if missing: vendors are always built with NDEBUG (decision 5), but the default
# configuration flags (e.g. CMAKE_C_FLAGS_RELWITHDEBINFO) already carry -DNDEBUG, and a second one must not
# be added. Returns, in <out>, the compile options that add -DNDEBUG for the language/configuration pairs
# whose flags lack it (empty with the default RelWithDebInfo build). A language that is not enabled yet
# (CXX is enabled inside the vendor section) and has no flags of its own follows the C decision.
function(_img2_vendor_ndebug_options _out)
	set(_opts "")
	get_property(_langs GLOBAL PROPERTY ENABLED_LANGUAGES)
	if(CMAKE_CONFIGURATION_TYPES)
		set(_cfgs ${CMAKE_CONFIGURATION_TYPES})
	else()
		set(_cfgs "${CMAKE_BUILD_TYPE}")
	endif()
	foreach(_cfg IN LISTS _cfgs)
		string(TOUPPER "${_cfg}" _ucfg)
		set(_c_has FALSE)
		foreach(_lang C CXX)
			if(NOT _lang IN_LIST _langs AND NOT DEFINED CMAKE_${_lang}_FLAGS_${_ucfg} AND NOT DEFINED CMAKE_${_lang}_FLAGS)
				set(_has ${_c_has})
			elseif(" ${CMAKE_${_lang}_FLAGS_${_ucfg}} ${CMAKE_${_lang}_FLAGS} " MATCHES "[ \t][-/]DNDEBUG[ \t]")
				set(_has TRUE)
			else()
				set(_has FALSE)
			endif()
			if(_lang STREQUAL "C")
				set(_c_has ${_has})
			endif()
			if(NOT _has)
				if(CMAKE_CONFIGURATION_TYPES)
					list(APPEND _opts "$<$<AND:$<COMPILE_LANGUAGE:${_lang}>,$<CONFIG:${_cfg}>>:-DNDEBUG>")
				else()
					list(APPEND _opts "$<$<COMPILE_LANGUAGE:${_lang}>:-DNDEBUG>")
				endif()
			endif()
		endforeach()
	endforeach()
	set(${_out} "${_opts}" PARENT_SCOPE)
endfunction()

# Vendor section: everything configured between begin and end (vendor subdirectories, try_compile probes)
# sees the executable link flags (-static, stack size) in CMAKE_EXE_LINKER_FLAGS, exactly as before the
# flags moved onto the executables. Subdirectories capture the value at add_subdirectory() time; the
# top-level targets created after img2_vendor_section_end() see the restored value.
# Inside the section only (every value is saved by begin and restored by end; unset when it was empty):
#   BUILD_SHARED_LIBS OFF (normal variable), CMAKE_POSITION_INDEPENDENT_CODE ${IMG2_POSITION_INDEPENDENT_CODE},
#   CMAKE_POLICY_DEFAULT_CMP0077/CMP0126/CMP0125 NEW (vendor option()/set(CACHE)/find_* honour the normal
#   knob variables set by cmake/vendor/*.cmake, so nothing lands in a consumer's cache), and the directory
#   COMPILE_OPTIONS -O3 -g, the C/C++ IMG2_HARDENING_FLAGS (+ -DNDEBUG where missing). The ExternalProject
#   vendors (libjpeg-turbo, aom) get the hardening flags through CMAKE_C_FLAGS/CMAKE_CXX_FLAGS instead
#   (cmake/vendor/Img2VendorJpeg.cmake, Img2VendorHeif.cmake). COMPILE_OPTIONS initialise the target property at
#   target creation and are inherited by subdirectories at add_subdirectory() time, so the vendor targets
#   compile exactly as with the former global add_compile_options(-O3 -g), and nothing reaches our targets
#   or a consumer's.
set(_IMG2_VENDOR_SECTION_VARS BUILD_SHARED_LIBS CMAKE_POSITION_INDEPENDENT_CODE
	CMAKE_POLICY_DEFAULT_CMP0077 CMAKE_POLICY_DEFAULT_CMP0126 CMAKE_POLICY_DEFAULT_CMP0125)
macro(img2_vendor_section_begin)
	set(_IMG2_SAVED_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
	string(JOIN " " _img2_lf ${IMG2_EXE_LINK_FLAGS})
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${_img2_lf}")   # vendors see today's -static/stack flags
	foreach(_img2_v IN LISTS _IMG2_VENDOR_SECTION_VARS)
		set(_IMG2_SAVED_${_img2_v} "${${_img2_v}}")
	endforeach()
	get_property(_IMG2_SAVED_DIR_COMPILE_OPTIONS DIRECTORY PROPERTY COMPILE_OPTIONS)
	set(BUILD_SHARED_LIBS OFF)
	set(CMAKE_POSITION_INDEPENDENT_CODE ${IMG2_POSITION_INDEPENDENT_CODE})
	set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
	set(CMAKE_POLICY_DEFAULT_CMP0126 NEW)
	set(CMAKE_POLICY_DEFAULT_CMP0125 NEW)
	set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS -O3 -g)
	# [F32] the vendored parsers get the same hardening flags as our sources (C/C++ only: never the assembler
	# sources; no warning flags)
	foreach(_img2_hf IN LISTS IMG2_HARDENING_FLAGS)
		set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "$<$<COMPILE_LANGUAGE:C,CXX>:${_img2_hf}>")
	endforeach()
	_img2_vendor_ndebug_options(_img2_ndebug_opts)
	if(_img2_ndebug_opts)
		set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS ${_img2_ndebug_opts})
	endif()
endmacro()
macro(img2_vendor_section_end)
	set(CMAKE_EXE_LINKER_FLAGS "${_IMG2_SAVED_EXE_LINKER_FLAGS}")
	foreach(_img2_v IN LISTS _IMG2_VENDOR_SECTION_VARS)
		if("${_IMG2_SAVED_${_img2_v}}" STREQUAL "")
			unset(${_img2_v})
		else()
			set(${_img2_v} "${_IMG2_SAVED_${_img2_v}}")
		endif()
		unset(_IMG2_SAVED_${_img2_v})
	endforeach()
	set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "${_IMG2_SAVED_DIR_COMPILE_OPTIONS}")
endmacro()
