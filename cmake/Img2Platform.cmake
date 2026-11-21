# Purpose: Platform link settings: static linking, macOS libc++ selection, stack size.
# Inputs:  IMG2_ENABLE_STATIC_LIBCXX, UNIX/APPLE/WIN32.
# Outputs: BUILD_SHARED_LIBS, CMAKE_FIND_LIBRARY_SUFFIXES, CMAKE_EXE_LINKER_FLAGS, STATIC_LIBCXX_LIBS, DYNAMIC_LIBCXX_LIBS.

# Static linking preference
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)

# Platform-specific static linking
# macOS doesn't support fully static binaries (libSystem must be dynamic)
# Only link third-party libraries statically on macOS
if(UNIX AND NOT APPLE)
	# Linux: Full static linking
	set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")

elseif(APPLE)
	# macOS: Static link third-party libs
	# Only search for .a files to force static linking
	set(CMAKE_FIND_LIBRARY_SUFFIXES .a)

	# Optionally find and link static libc++ from LLVM (Homebrew)
	if(IMG2_ENABLE_STATIC_LIBCXX)
		find_library(LIBCXX_STATIC NAMES c++ libc++.a
			PATHS /usr/local/opt/llvm/lib/c++ /usr/local/Cellar/llvm/*/lib/c++
			NO_DEFAULT_PATH)
		find_library(LIBCXXABI_STATIC NAMES c++abi libc++abi.a
			PATHS /usr/local/opt/llvm/lib/c++ /usr/local/Cellar/llvm/*/lib/c++
			NO_DEFAULT_PATH)

		if(LIBCXX_STATIC AND LIBCXXABI_STATIC)
			# Statically link libc++ and libc++abi
			set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -nostdlib++")
			set(STATIC_LIBCXX_LIBS ${LIBCXX_STATIC} ${LIBCXXABI_STATIC})
			message(STATUS "Using static libc++: ${LIBCXX_STATIC}")
			message(STATUS "Using static libc++abi: ${LIBCXXABI_STATIC}")

		else()
			message(WARNING "Static libc++ requested but not found, using dynamic linking")
			# Fall back to dynamic libc++
			set(DYNAMIC_LIBCXX_LIBS c++)
		endif()

	else()
		# Use dynamic libc++ (default)
		set(DYNAMIC_LIBCXX_LIBS c++)
	endif()
endif()

# Stack size linker flags
if(UNIX AND NOT APPLE)
	# Linux
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,stack-size=8388608")

elseif(APPLE)
	# macOS
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-stack_size,0x800000")

elseif(WIN32)
	# Windows
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /STACK:8388608")
endif()
