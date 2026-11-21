# Consumer smoke test driver (ctest: img2_consumer_find_package, img2_consumer_subdir).
#   cmake -DMODE=package|subdir -DSRC=<img2 source dir> -DBIN=<img2 build dir> -DGENERATOR=<generator>
#         -DIMG=<png file> -P cmake/scripts/img2_consumer_smoke.cmake
# Everything is written below ${BIN}/consumer-tests (never /tmp).
# package: cmake --install ${BIN} --component img2_development into consumer-tests/prefix, move the prefix to
#          consumer-tests/prefix-moved ([SL3] the original location no longer exists), grep the installed text
#          files for build/source paths, then configure/build/run src/tests/consumer with find_package(img2).
# subdir:  configure/build/run src/tests/consumer with add_subdirectory(${SRC}) in the persistent
#          consumer-tests/subdir, then audit the consumer's CMakeCache.txt for entries leaked by img2.
# The consumer's "img2 consumer OK" line is printed only after every check passed (ctest matches it).

foreach(_v MODE SRC BIN GENERATOR IMG)
	if(NOT DEFINED ${_v} OR "${${_v}}" STREQUAL "")
		message(FATAL_ERROR "img2_consumer_smoke: -D${_v}=... is required")
	endif()
endforeach()

set(_ct "${BIN}/consumer-tests")
set(_consumer_src "${SRC}/src/tests/consumer")
file(MAKE_DIRECTORY "${_ct}")

# Run a command; FATAL_ERROR with its output on failure; the output is returned in _run_out
function(_img2_run what)
	execute_process(COMMAND ${ARGN} RESULT_VARIABLE _rv OUTPUT_VARIABLE _out ERROR_VARIABLE _out)
	if(NOT _rv EQUAL 0)
		message(FATAL_ERROR "img2_consumer_smoke: ${what} failed (${_rv}):\n${_out}")
	endif()
	set(_run_out "${_out}" PARENT_SCOPE)
endfunction()

if(MODE STREQUAL "package")
	set(_prefix "${_ct}/prefix")
	set(_moved "${_ct}/prefix-moved")
	file(REMOVE_RECURSE "${_prefix}" "${_moved}")
	_img2_run("install (component img2_development)"
		${CMAKE_COMMAND} --install "${BIN}" --prefix "${_prefix}" --component img2_development)
	message(STATUS "${_run_out}")

	# [SL3] relocation: copy the prefix, then remove the original location
	file(COPY "${_prefix}/" DESTINATION "${_moved}")
	file(REMOVE_RECURSE "${_prefix}")

	file(GLOB_RECURSE _installed RELATIVE "${_moved}" "${_moved}/*")
	list(SORT _installed)
	string(REPLACE ";" "\n  " _installed_str "${_installed}")
	message(STATUS "installed (moved prefix):\n  ${_installed_str}")
	foreach(_need include/img2/img2.h include/img2/img2_config.h)
		if(NOT EXISTS "${_moved}/${_need}")
			message(FATAL_ERROR "img2_consumer_smoke: ${_need} missing from the installed package")
		endif()
	endforeach()
	file(GLOB_RECURSE _cfg "${_moved}/*/img2Config.cmake")
	file(GLOB_RECURSE _cfgv "${_moved}/*/img2ConfigVersion.cmake")
	file(GLOB_RECURSE _lib "${_moved}/*/libimg2.a" "${_moved}/*/img2.lib")
	if(NOT _cfg OR NOT _cfgv OR NOT _lib)
		message(FATAL_ERROR "img2_consumer_smoke: img2Config.cmake / img2ConfigVersion.cmake / libimg2.a missing")
	endif()

	# [SL3] path leaks: no installed text file may name the build tree, the source tree or the original prefix
	file(GLOB_RECURSE _texts "${_moved}/*.cmake" "${_moved}/*.h")
	foreach(_f IN LISTS _texts)
		file(READ "${_f}" _content)
		foreach(_needle "${BIN}" "${SRC}" "${_prefix}")
			string(FIND "${_content}" "${_needle}" _pos)
			if(_pos GREATER -1)
				message(FATAL_ERROR "img2_consumer_smoke: build-tree path leaked into installed file ${_f}: ${_needle}")
			endif()
		endforeach()
	endforeach()
	list(LENGTH _texts _ntexts)
	message(STATUS "path-leak check: ${_ntexts} installed text files clean")

	set(_cbin "${_ct}/pkg")
	file(REMOVE_RECURSE "${_cbin}")
	_img2_run("consumer configure (package)"
		${CMAKE_COMMAND} -S "${_consumer_src}" -B "${_cbin}" -G "${GENERATOR}"
		-DIMG2_CONSUMER_MODE=package "-DCMAKE_PREFIX_PATH=${_moved}")
	message(STATUS "${_run_out}")
	_img2_run("consumer build (package)" ${CMAKE_COMMAND} --build "${_cbin}" --parallel 8)
	message(STATUS "${_run_out}")

elseif(MODE STREQUAL "subdir")
	set(_cbin "${_ct}/subdir")   # persistent: re-runs are incremental
	_img2_run("consumer configure (subdir)"
		${CMAKE_COMMAND} -S "${_consumer_src}" -B "${_cbin}" -G "${GENERATOR}"
		-DIMG2_CONSUMER_MODE=subdir "-DIMG2_SOURCE_DIR=${SRC}")
	_img2_run("consumer build (subdir)" ${CMAKE_COMMAND} --build "${_cbin}" --parallel 32)
	message(STATUS "consumer build (subdir): ok")

	# Cache leak audit: nothing img2 configures may land in the consumer's cache
	file(STRINGS "${_cbin}/CMakeCache.txt" _cache_lines REGEX "^[A-Za-z0-9_.+-]+:[A-Z]+=")
	set(_forbidden BUILD_SHARED_LIBS ZLIB_LIBRARY JPEG_LIBRARY WITH_LIBSHARPYUV WebP_LIBRARY
		ZLIB_COMPAT PNG_TESTS WEBP_BUILD_CWEBP ENABLE_SDL ENABLE_PLUGIN_LOADING HWY_FORCE_STATIC_LIBS
		JPEGXL_FORCE_SYSTEM_HWY IMG2_BUILD_CLI IMG2_BUILD_TESTS IMG2_INSTALL)
	# docs/libimg2.md "Known cache entries": created by the vendors' own set(CACHE) with upstream defaults
	# (the effective value is our normal variable, CMP0126 NEW)
	set(_known HWY_FORCE_STATIC_LIBS JPEGXL_FORCE_SYSTEM_HWY)
	# img2's user options and check results; the consumer's own -D entries
	set(_img2_allowed_re "^IMG2_(ENABLE_|USE_SYSTEM_)|^IMG2_(WERROR|POSITION_INDEPENDENT_CODE|FAT_ARCHIVE|FORTIFY_PREDEFINED|HAVE_SECURE_GETENV|CONSUMER_MODE|SOURCE_DIR)$")
	set(_violations "")
	set(_residual "")
	foreach(_l IN LISTS _cache_lines)
		string(REGEX MATCH "^([A-Za-z0-9_.+-]+):([A-Z]+)=" _m "${_l}")
		if(NOT _m)
			continue()   # tail of a value that contained ';' (split by the list)
		endif()
		set(_name "${CMAKE_MATCH_1}")
		set(_type "${CMAKE_MATCH_2}")
		if(_name MATCHES "^CMAKE_" OR _name MATCHES "^img2_consumer_")
			continue()
		endif()
		string(REPLACE ";" "," _l_print "${_l}")   # list-valued entries on one line
		list(APPEND _residual "${_l_print}")
		if(_name IN_LIST _known)
			continue()
		endif()
		if(_name IN_LIST _forbidden OR _name MATCHES "^HAVE_JPEGTURBO_DUAL_MODE_")
			list(APPEND _violations "${_l}")
		elseif(_name MATCHES "^IMG2_" AND NOT _name MATCHES "${_img2_allowed_re}")
			list(APPEND _violations "${_l}")
		endif()
	endforeach()
	list(LENGTH _residual _nres)
	string(REPLACE ";" "\n  " _residual_str "${_residual}")
	message(STATUS "cache leak audit: ${_nres} residual non-CMAKE_ entries in the consumer cache:\n  ${_residual_str}")
	if(_violations)
		string(REPLACE ";" "\n  " _viol_str "${_violations}")
		message(FATAL_ERROR "img2_consumer_smoke: img2 leaked cache entries into the consumer:\n  ${_viol_str}")
	endif()
	message(STATUS "cache leak audit: ok")

else()
	message(FATAL_ERROR "img2_consumer_smoke: unknown MODE '${MODE}' (package|subdir)")
endif()

_img2_run("consumer run (${MODE})" "${_cbin}/img2_consumer" "${IMG}")
message(STATUS "${_run_out}")
