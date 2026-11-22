# Purpose: Vendored-library manifest ([F37]): which third-party code is inside the fat libimg2.a, at which commit.
# Inputs:  PROJECT_SOURCE_DIR/.gitmodules, the vendor/ submodule checkouts (git), vendored targets (TARGET x) at
#          call time, PROJECT_VERSION.
# Outputs: function img2_write_vendor_manifest(<file>): writes the manifest at configure time.
#
# One tab-separated row per vendor/ submodule listed in .gitmodules:
#   name  built  path  pinned  checked_out  describe  state  url  notes
#   built        yes = compiled into this build (the vendored target exists), no = feature off / host library
#   pinned       commit recorded in the superproject's HEAD tree (gitlink, `git ls-tree HEAD <path>`)
#   checked_out  `git -C <path> rev-parse HEAD`; differs from pinned when the submodule was moved
#   describe     `git -C <path> describe --tags --always` (upstream version tag where the repository has one)
#   state        clean / modified (tracked files differ from checked_out) / unknown (no git or no checkout)
# Paths are relative to the img2 source tree; the file never names the build, source or install tree, so it is
# installable as is. Re-run CMake to refresh it after moving a submodule (gitlinks are not a configure dependency).

# name -> vendored target whose existence means "compiled into this build"; header-only libraries are always in
set(_IMG2_MANIFEST_TARGETS
	"stb=" "qoi=" "nanosvg=" "jpeg-quantsmooth=jpegqs" "zlib-ng=zlib-ng" "libjpeg-turbo=libjpeg-turbo-ext"
	"libpng=png_static" "giflib=vendor_giflib" "libwebp=webp" "libtiff=tiff" "lcms2=vendor_lcms2"
	"libraw=vendor_libraw" "libjxl=jxl" "brotli=brotlicommon" "highway=hwy" "libde265=de265" "libheif=heif"
	"kvazaar=vendor_kvazaar" "openjpeg=openjp2" "aom=aom-ext")
set(_IMG2_MANIFEST_NOTES
	"libpng=built from a configure-time copy with vendor/patches/libpng-1.6.59-apng.patch applied"
	"libjpeg-turbo=ExternalProject build, jpeg8 ABI"
	"aom=ExternalProject build"
	"nanosvg=header-only, compiled into img2"
	"stb=header-only (stb_image, stb_image_resize2, stb_truetype), compiled into img2"
	"qoi=header-only, compiled into img2")

function(_img2_manifest_lookup _list _key _out)
	set(_v "")
	foreach(_e IN LISTS ${_list})
		if(_e MATCHES "^${_key}=(.*)$")
			set(_v "${CMAKE_MATCH_1}")
			break()
		endif()
	endforeach()
	set(${_out} "${_v}" PARENT_SCOPE)
endfunction()

function(_img2_manifest_git _out)
	set(_v "unknown")
	if(GIT_EXECUTABLE)
		execute_process(COMMAND "${GIT_EXECUTABLE}" ${ARGN} RESULT_VARIABLE _rv OUTPUT_VARIABLE _o ERROR_QUIET
			OUTPUT_STRIP_TRAILING_WHITESPACE)
		if(_rv EQUAL 0)
			set(_v "${_o}")
		endif()
	endif()
	set(${_out} "${_v}" PARENT_SCOPE)
endfunction()

function(img2_write_vendor_manifest _file)
	set(_gm "${PROJECT_SOURCE_DIR}/.gitmodules")
	find_package(Git QUIET)
	set(_txt "# img2 ${PROJECT_VERSION}: vendored libraries (generated at configure time; see docs/libimg2.md)\n")
	string(APPEND _txt "# Symbols of the vendored libraries are not prefixed: a consumer that links its own copy of one of\n")
	string(APPEND _txt "# them next to libimg2.a gets duplicate-symbol errors or mixed versions.\n")
	string(APPEND _txt "# name\tbuilt\tpath\tpinned\tchecked_out\tdescribe\tstate\turl\tnotes\n")
	if(NOT EXISTS "${_gm}")
		string(APPEND _txt "# .gitmodules not found: source tree is not a git checkout, commits unknown\n")
		file(WRITE "${_file}" "${_txt}")
		return()
	endif()
	set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_gm}")
	file(STRINGS "${_gm}" _lines)
	set(_paths "")
	foreach(_l IN LISTS _lines)
		if(_l MATCHES "^[ \t]*path[ \t]*=[ \t]*(vendor/[^ \t]+)[ \t]*$")
			list(APPEND _paths "${CMAKE_MATCH_1}")
			set(_url_${CMAKE_MATCH_1} "")
			set(_cur "${CMAKE_MATCH_1}")
		elseif(_l MATCHES "^[ \t]*url[ \t]*=[ \t]*([^ \t]+)[ \t]*$" AND DEFINED _cur)
			set(_url_${_cur} "${CMAKE_MATCH_1}")
		elseif(_l MATCHES "^\\[")
			unset(_cur)
		endif()
	endforeach()
	foreach(_p IN LISTS _paths)
		get_filename_component(_name "${_p}" NAME)
		_img2_manifest_lookup(_IMG2_MANIFEST_TARGETS "${_name}" _tgt)
		_img2_manifest_lookup(_IMG2_MANIFEST_NOTES "${_name}" _note)
		if(_tgt STREQUAL "" OR TARGET ${_tgt})
			set(_built yes)
		else()
			set(_built no)
		endif()
		_img2_manifest_git(_pinned -C "${PROJECT_SOURCE_DIR}" ls-tree HEAD -- "${_p}")
		if(_pinned MATCHES "^160000 commit ([0-9a-f]+)\t")
			set(_pinned "${CMAKE_MATCH_1}")
		else()
			set(_pinned "unknown")
		endif()
		set(_dir "${PROJECT_SOURCE_DIR}/${_p}")
		set(_head "unknown")
		set(_desc "unknown")
		set(_state "unknown")
		if(EXISTS "${_dir}/.git")
			_img2_manifest_git(_head -C "${_dir}" rev-parse HEAD)
			_img2_manifest_git(_desc -C "${_dir}" describe --tags --always)
			_img2_manifest_git(_st -C "${_dir}" status --porcelain --untracked-files=no --ignore-submodules=all)
			if(_st STREQUAL "")
				set(_state clean)
			elseif(NOT _st STREQUAL "unknown")
				set(_state modified)
			endif()
		endif()
		string(APPEND _txt "${_name}\t${_built}\t${_p}\t${_pinned}\t${_head}\t${_desc}\t${_state}\t${_url_${_p}}\t${_note}\n")
	endforeach()
	# Write only on change (no needless rebuild/reinstall of dependants)
	set(_old "")
	if(EXISTS "${_file}")
		file(READ "${_file}" _old)
	endif()
	if(NOT _old STREQUAL _txt)
		file(WRITE "${_file}" "${_txt}")
	endif()
endfunction()
