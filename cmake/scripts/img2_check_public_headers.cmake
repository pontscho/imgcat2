# Public header guard (cmake -P).
#   cmake -DROOT=<repo> -P cmake/scripts/img2_check_public_headers.cmake
#
# Every include/img2/*.h must be feature independent: it must not contain the text HAVE_ (which also
# covers IMG2_HAVE_), and its only conditional directives (#if/#ifdef/#ifndef/#elif) may be its own
# include guard (#ifndef IMG2_<NAME>_H) and __cplusplus tests (#ifdef __cplusplus, #if defined(__cplusplus)).
#
# Files are read whole (file(READ)), not with file(STRINGS): CMake list splitting on ';' and '['
# would merge or split lines and hide matches.

if(NOT DEFINED ROOT)
	message(FATAL_ERROR "usage: cmake -DROOT=<repo> -P img2_check_public_headers.cmake")
endif()

file(GLOB _files LIST_DIRECTORIES false "${ROOT}/include/img2/*.h")
list(SORT _files)
if(NOT _files)
	message(FATAL_ERROR "img2_check_public_headers: no headers in ${ROOT}/include/img2")
endif()

set(_violations "")
set(_cond_re "\n[ \t]*#[ \t]*(if|ifdef|ifndef|elif)[^\n]*")
foreach(_f IN LISTS _files)
	file(RELATIVE_PATH _rel "${ROOT}" "${_f}")
	get_filename_component(_name "${_f}" NAME_WE)
	string(TOUPPER "${_name}" _upper)
	set(_guard "IMG2_${_upper}_H")

	file(READ "${_f}" _c)
	if(_c MATCHES "HAVE_")
		list(APPEND _violations "${_rel}: forbidden text HAVE_")
	endif()

	string(REGEX MATCHALL "${_cond_re}" _conds "\n${_c}")
	set(_guard_seen FALSE)
	foreach(_d IN LISTS _conds)
		string(STRIP "${_d}" _d)
		string(REGEX REPLACE "[ \t]+" " " _d "${_d}")
		string(REGEX REPLACE "^# " "#" _d "${_d}")
		string(REGEX REPLACE "[ \t]*/\\*.*$" "" _d "${_d}")
		if(_d STREQUAL "#ifndef ${_guard}" AND NOT _guard_seen)
			set(_guard_seen TRUE)
		elseif(_d STREQUAL "#ifdef __cplusplus" OR _d STREQUAL "#if defined(__cplusplus)")
			# allowed
		else()
			list(APPEND _violations "${_rel}: forbidden conditional '${_d}'")
		endif()
	endforeach()
	if(NOT _guard_seen)
		list(APPEND _violations "${_rel}: missing include guard #ifndef ${_guard}")
	endif()
endforeach()

if(_violations)
	list(LENGTH _violations _n)
	foreach(_v IN LISTS _violations)
		message(STATUS "PUBLIC-HEADER: ${_v}")
	endforeach()
	message(FATAL_ERROR "img2_check_public_headers: ${_n} violation(s)")
endif()
list(LENGTH _files _nf)
message(STATUS "img2_check_public_headers: ok (${_nf} headers)")
