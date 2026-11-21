# Purpose: resvg (optional, preferred SVG decoder; host library).
# Inputs:  IMG2_ENABLE_RESVG.
# Outputs: RESVG_FOUND, RESVG_LIBRARIES, RESVG_INCLUDE_DIRS, img2_feature(RESVG).

# resvg (optional, preferred SVG decoder)
if(IMG2_ENABLE_RESVG)
	# Find static library explicitly on macOS
	if(APPLE)
		find_library(RESVG_LIBRARY NAMES resvg)
		find_path(RESVG_INCLUDE_DIR resvg/resvg.h)
		if(RESVG_LIBRARY AND RESVG_INCLUDE_DIR)
			set(RESVG_LIBRARIES ${RESVG_LIBRARY})
			set(RESVG_INCLUDE_DIRS ${RESVG_INCLUDE_DIR})
			set(RESVG_FOUND TRUE)
			# Get version from pkg-config if available
			execute_process(COMMAND pkg-config --modversion resvg
				OUTPUT_VARIABLE RESVG_VERSION
				OUTPUT_STRIP_TRAILING_WHITESPACE
				ERROR_QUIET)
		endif()

	else()
		pkg_check_modules(RESVG resvg)
	endif()

	if(RESVG_FOUND)
		img2_feature(RESVG LIBS ${RESVG_LIBRARIES} INCLUDE_DIRS ${RESVG_INCLUDE_DIRS} SYSTEM_LIBS ${RESVG_LIBRARIES})
		img2_warn_host_dependency("resvg (IMG2_ENABLE_RESVG)")
		message(STATUS "resvg found: ${RESVG_VERSION} (preferred SVG decoder)")

	else()
		message(STATUS "resvg not found, using nanosvg fallback")
		set(IMG2_ENABLE_RESVG OFF)
	endif()
endif()
