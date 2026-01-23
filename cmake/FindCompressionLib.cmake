# FindCompressionLib.cmake
# Priority-based compression library detection: libdeflate > zlib-ng > zlib
#
# Sets the following variables:
#   COMPRESSION_LIB_FOUND         - TRUE if a compression library was found
#   COMPRESSION_LIB_TYPE          - "libdeflate", "zlib-ng", or "zlib"
#   ZLIB_LIBRARIES                - Libraries to link
#   ZLIB_INCLUDE_DIRS             - Include directories
#   USE_LIBDEFLATE_COMPAT         - TRUE if using libdeflate with compatibility shim

find_package(PkgConfig REQUIRED)

set(COMPRESSION_LIB_FOUND FALSE)
set(USE_LIBDEFLATE_COMPAT FALSE)

# Priority 1: Try zlib-ng
# NOTE: libdeflate one-shot API is incompatible with zlib's streaming API
#       required by libpng. A compatibility shim is not practical for streaming use.
#       zlib-ng provides 10-30% performance improvement and is fully compatible.
message(STATUS "Searching for compression libraries (priority: zlib-ng > zlib)")

pkg_check_modules(ZLIB_NG zlib-ng)

if(ZLIB_NG_FOUND)
	set(COMPRESSION_LIB_TYPE "zlib-ng")
	set(COMPRESSION_LIB_FOUND TRUE)
	set(ZLIB_LIBRARIES ${ZLIB_NG_LIBRARIES})
	set(ZLIB_INCLUDE_DIRS ${ZLIB_NG_INCLUDE_DIRS})
	message(STATUS "  Found zlib-ng: ${ZLIB_LIBRARIES}")
endif()

# Priority 2: Fallback to standard zlib
if(NOT COMPRESSION_LIB_FOUND)
	# On macOS, find static library explicitly
	if(APPLE)
		find_library(ZLIB_LIBRARY NAMES libz.a z REQUIRED
			HINTS /usr/local/opt/zlib/lib /usr/local/lib)
		find_path(ZLIB_INCLUDE_DIR zlib.h
			HINTS /usr/local/opt/zlib/include /usr/local/include)

		if(ZLIB_LIBRARY AND ZLIB_INCLUDE_DIR)
			set(ZLIB_LIBRARIES ${ZLIB_LIBRARY})
			set(ZLIB_INCLUDE_DIRS ${ZLIB_INCLUDE_DIR})
			set(COMPRESSION_LIB_TYPE "zlib")
			set(COMPRESSION_LIB_FOUND TRUE)
			message(STATUS "  Found zlib: ${ZLIB_LIBRARIES}")
		endif()
	else()
		# Try pkg-config first
		pkg_check_modules(ZLIB zlib)

		if(ZLIB_FOUND)
			set(ZLIB_LIBRARIES ${ZLIB_LIBRARIES})
			set(ZLIB_INCLUDE_DIRS ${ZLIB_INCLUDE_DIRS})
			set(COMPRESSION_LIB_TYPE "zlib")
			set(COMPRESSION_LIB_FOUND TRUE)
			message(STATUS "  Found zlib: ${ZLIB_LIBRARIES}")
		else()
			# Fallback to find_package
			find_package(ZLIB REQUIRED)
			set(ZLIB_LIBRARIES ${ZLIB_LIBRARIES})
			set(ZLIB_INCLUDE_DIRS ${ZLIB_INCLUDE_DIRS})
			set(COMPRESSION_LIB_TYPE "zlib")
			set(COMPRESSION_LIB_FOUND TRUE)
			message(STATUS "  Found zlib: ${ZLIB_LIBRARIES}")
		endif()
	endif()
endif()

# Error if no compression library found
if(NOT COMPRESSION_LIB_FOUND)
	message(FATAL_ERROR "No compression library found. Please install zlib, zlib-ng, or libdeflate.")
endif()
