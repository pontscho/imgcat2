# Symbol guard (cmake -P).
#   cmake -DNM=<nm> -DPREFIX=img2_ [-DARCHIVE=<lib.a>] [-DOBJ_LIST=<obj;obj>] [-DFORBIDDEN=<sym;...>]
#         -P cmake/scripts/img2_check_symbols.cmake
#
# Runs `${NM} -A -P -g` on the archive and/or each listed object.
# - Every defined global (types T D B R C V W G S) must start with ${PREFIX}.
# - With FORBIDDEN set, any undefined (U) reference to a listed symbol fails.
# On a hit: message(FATAL_ERROR) listing "member: symbol".
# IMG2_SYMBOL_ALLOWLIST is a hook for exemptions and must stay empty.

set(IMG2_SYMBOL_ALLOWLIST "")

if(NOT DEFINED NM OR NOT DEFINED PREFIX)
	message(FATAL_ERROR "usage: cmake -DNM=<nm> -DPREFIX=<prefix> [-DARCHIVE=<a>] [-DOBJ_LIST=<o;o>] [-DFORBIDDEN=<s;s>] -P img2_check_symbols.cmake")
endif()

set(_inputs "")
if(DEFINED ARCHIVE AND NOT ARCHIVE STREQUAL "")
	list(APPEND _inputs "${ARCHIVE}")
endif()
if(DEFINED OBJ_LIST AND NOT OBJ_LIST STREQUAL "")
	list(APPEND _inputs ${OBJ_LIST})
endif()
if(NOT _inputs)
	message(FATAL_ERROR "img2_check_symbols: neither ARCHIVE nor OBJ_LIST given")
endif()

set(_violations "")
set(_ndefined 0)
foreach(_in IN LISTS _inputs)
	if(NOT EXISTS "${_in}")
		message(FATAL_ERROR "img2_check_symbols: input not found: ${_in}")
	endif()
	execute_process(COMMAND "${NM}" -A -P -g "${_in}"
		OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
	if(NOT _rc EQUAL 0)
		message(FATAL_ERROR "img2_check_symbols: ${NM} failed on ${_in} (${_rc}): ${_err}")
	endif()
	# One line per symbol: "<file>[<member>]: <name> <type> [<value> [<size>]]" (or "<file>: ..." for objects).
	# Symbol names never contain ';' or '[' in C, but member names could; replace them before splitting.
	string(REPLACE ";" "\\;" _out "${_out}")
	string(REPLACE "\n" ";" _lines "${_out}")
	foreach(_l IN LISTS _lines)
		if(NOT _l MATCHES "^(.*): ([^ ]+) ([A-Za-z]) ?")
			continue()
		endif()
		set(_member "${CMAKE_MATCH_1}")
		set(_sym "${CMAKE_MATCH_2}")
		set(_type "${CMAKE_MATCH_3}")
		if(_type MATCHES "^[TDBRCVWGS]$")
			math(EXPR _ndefined "${_ndefined} + 1")
			string(FIND "${_sym}" "${PREFIX}" _pos)
			if(NOT _pos EQUAL 0)
				list(FIND IMG2_SYMBOL_ALLOWLIST "${_sym}" _allowed)
				if(_allowed EQUAL -1)
					list(APPEND _violations "${_member}: ${_sym} (${_type}, missing prefix ${PREFIX})")
				endif()
			endif()
		elseif(_type STREQUAL "U" AND DEFINED FORBIDDEN AND NOT FORBIDDEN STREQUAL "")
			list(FIND FORBIDDEN "${_sym}" _bad)
			if(NOT _bad EQUAL -1)
				list(APPEND _violations "${_member}: ${_sym} (forbidden reference)")
			endif()
		endif()
	endforeach()
endforeach()

if(_violations)
	list(LENGTH _violations _n)
	foreach(_v IN LISTS _violations)
		message(STATUS "SYMBOLS: ${_v}")
	endforeach()
	message(FATAL_ERROR "img2_check_symbols: ${_n} violation(s)")
endif()
if(_ndefined EQUAL 0)
	message(FATAL_ERROR "img2_check_symbols: no defined global symbols found (wrong input?)")
endif()
message(STATUS "img2_check_symbols: ok (${_ndefined} defined globals, prefix ${PREFIX})")
