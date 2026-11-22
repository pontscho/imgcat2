# [F37] Duplicate strong global symbols in the fat libimg2.a.
#   cmake -DNM=<nm> -DARCHIVE=<libimg2.a> [-DALLOWLIST=<file>] -P cmake/scripts/img2_check_duplicate_symbols.cmake
# Every strong global definition (nm types T D B R: text, data, bss, read-only data) must come from exactly one
# archive member. A second definition would be silently ignored by the linker (it stops at the first member
# that resolves the symbol), so a symbol clash between two vendored libraries (or between a vendored library and
# img2) would pick one implementation by merge order. Weak (W/V), common (C), unique (u) and indirect (i)
# symbols are not checked.
# ALLOWLIST: one symbol name per line, '#' starts a comment; each entry must be documented there and must
# still be duplicated (a stale entry fails too, so the list cannot rot).
# On failure ARCHIVE is deleted, so the next build re-runs the merge and this check.

foreach(_v NM ARCHIVE)
	if(NOT DEFINED ${_v} OR "${${_v}}" STREQUAL "")
		message(FATAL_ERROR "img2_check_duplicate_symbols: -D${_v}=... is required")
	endif()
endforeach()
if(NOT EXISTS "${ARCHIVE}")
	message(FATAL_ERROR "img2_check_duplicate_symbols: archive does not exist: ${ARCHIVE}")
endif()

set(_allowed "")
if(DEFINED ALLOWLIST AND NOT ALLOWLIST STREQUAL "")
	file(STRINGS "${ALLOWLIST}" _lines)
	foreach(_l IN LISTS _lines)
		string(REGEX REPLACE "#.*$" "" _l "${_l}")
		string(STRIP "${_l}" _l)
		if(NOT _l STREQUAL "")
			list(APPEND _allowed "${_l}")
		endif()
	endforeach()
endif()

# -A: prefix every line with archive:member; -g: external symbols only; --defined-only; -P: POSIX format
# "<archive[member]>: <name> <type> <value> [<size>]" (GNU nm prints "archive[member]: ...").
execute_process(COMMAND "${NM}" -A -g -P --defined-only "${ARCHIVE}"
	RESULT_VARIABLE _rv OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rv EQUAL 0)
	message(FATAL_ERROR "img2_check_duplicate_symbols: ${NM} failed (${_rv}):\n${_err}")
endif()

# Pass 1: names of the strong globals (the member is the text before the first ": " of the line).
string(REGEX MATCHALL "[^\n]+" _rows "${_out}")
set(_syms "")
foreach(_r IN LISTS _rows)
	if(_r MATCHES "^.*: ([^ ]+) [TDBR] ")
		list(APPEND _syms "${CMAKE_MATCH_1}")
	endif()
endforeach()
list(LENGTH _syms _nstrong)

set(_seen "")
list(SORT _syms)
set(_prev "")
foreach(_s IN LISTS _syms)
	if(_s STREQUAL _prev AND NOT _s IN_LIST _seen)
		list(APPEND _seen "${_s}")
	endif()
	set(_prev "${_s}")
endforeach()

# Pass 2 (only when something is duplicated and not allow-listed): the defining members of each duplicate.
set(_bad "")
foreach(_s IN LISTS _seen)
	if(NOT _s IN_LIST _allowed)
		list(APPEND _bad "${_s}")
	endif()
endforeach()
set(_violations "")
if(_bad)
	foreach(_r IN LISTS _rows)
		if(_r MATCHES "^(.*): ([^ ]+) ([TDBR]) ")
			set(_m "${CMAKE_MATCH_1}")
			set(_n "${CMAKE_MATCH_2}")
			set(_t "${CMAKE_MATCH_3}")
			if(_n IN_LIST _bad)
				string(APPEND _violations "  ${_n} (${_t}) in ${_m}\n")
			endif()
		endif()
	endforeach()
endif()
set(_stale "")
foreach(_a IN LISTS _allowed)
	if(NOT _a IN_LIST _seen)
		string(APPEND _stale "  ${_a}\n")
	endif()
endforeach()

if(NOT _violations STREQUAL "" OR NOT _stale STREQUAL "")
	file(REMOVE "${ARCHIVE}")
	set(_msg "img2_check_duplicate_symbols: ${ARCHIVE} (removed)\n")
	if(NOT _violations STREQUAL "")
		string(APPEND _msg "strong global symbols defined in more than one member:\n${_violations}")
	endif()
	if(NOT _stale STREQUAL "")
		string(APPEND _msg "allow-list entries that are no longer duplicated (remove them from ${ALLOWLIST}):\n${_stale}")
	endif()
	message(FATAL_ERROR "${_msg}")
endif()
list(LENGTH _allowed _nallow)
message(STATUS "img2_check_duplicate_symbols: ${_nstrong} strong global definitions, no unexpected duplicates (${_nallow} allow-listed)")
