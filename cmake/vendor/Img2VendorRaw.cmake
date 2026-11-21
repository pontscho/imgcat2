# Purpose: LibRaw (optional): vendored or host.
# Inputs:  IMG2_ENABLE_RAW, IMG2_USE_SYSTEM_RAW, LCMS2_TARGET, JPEG/zlib results.
# Outputs: vendor_libraw, RAW_FOUND, RAW_LIBRARIES, RAW_INCLUDE_DIRS, RAW_LIB_TYPE, img2_feature(RAW).

# RAW (optional)
if(IMG2_ENABLE_RAW AND NOT IMG2_USE_SYSTEM_RAW)
	# LibRaw is C++ (only needed when the vendored copy is built)
	enable_language(CXX)

	# Little-CMS 2 (LibRaw color management): ${LCMS2_TARGET}, see "Little-CMS 2" above

	# LibRaw: pattern B (hand-written static target). Chosen over the LibRaw-cmake wrapper:
	# one pinned submodule instead of two (LibRaw-cmake has no release tags), and the
	# source list is simply upstream's Makefile.dist LIB_OBJECTS = src/**/*.cpp minus the
	# *_ph.cpp placeholder units (79 files). Thread-safe variant (libraw_r equivalent).
	# License: LibRaw is dual-licensed LGPL-2.1 or CDDL-1.0; imgcat2 uses it under
	# CDDL-1.0 (file-level copyleft, no LGPL relinking requirement for a static binary).
	set(LIBRAW_SRC_DIR "${PROJECT_SOURCE_DIR}/vendor/libraw")
	file(GLOB LIBRAW_SOURCES "${LIBRAW_SRC_DIR}/src/*.cpp" "${LIBRAW_SRC_DIR}/src/*/*.cpp")
	list(FILTER LIBRAW_SOURCES EXCLUDE REGEX "_ph\\.cpp$")
	add_library(vendor_libraw STATIC ${LIBRAW_SOURCES})
	target_include_directories(vendor_libraw PUBLIC ${LIBRAW_SRC_DIR})
	# kept for byte identity; was a global include_directories(): zlib-ng dirs before lcms2's (-I order as before)
	target_include_directories(vendor_libraw PRIVATE ${ZLIB_INCLUDE_DIRS})
	# USE_LCMS2 is PUBLIC: libraw_types.h includes lcms2.h under it (header consistency)
	target_compile_definitions(vendor_libraw
		PUBLIC USE_LCMS2
		PRIVATE USE_ZLIB USE_JPEG)
	if(TARGET libjpeg-turbo-ext)
		# jpeg8 ABI: jpeg_mem_src() is available (lossy DNG support)
		target_compile_definitions(vendor_libraw PRIVATE USE_JPEG8)
	endif()
	# Upstream builds LibRaw with -w; its warnings are not ours to fix
	target_compile_options(vendor_libraw PRIVATE -w)
	target_link_libraries(vendor_libraw PUBLIC ${LCMS2_TARGET} ${JPEG_LIBRARIES} ${ZLIB_LIBRARIES})
	set_target_properties(vendor_libraw PROPERTIES
		POSITION_INDEPENDENT_CODE ON
		CXX_CLANG_TIDY "")

	find_package(Threads REQUIRED)
	target_link_libraries(vendor_libraw PUBLIC Threads::Threads)

	# OpenMP, as with the previous host libraw (-fopenmp)
	find_package(OpenMP COMPONENTS CXX)
	if(OpenMP_CXX_FOUND)
		if(UNIX AND NOT APPLE)
			# Flag form so the gcc driver picks static libgomp under -static
			separate_arguments(_raw_omp_flags UNIX_COMMAND "${OpenMP_CXX_FLAGS}")
			target_compile_options(vendor_libraw PRIVATE ${_raw_omp_flags})
			target_link_options(vendor_libraw INTERFACE ${_raw_omp_flags})
		else()
			target_link_libraries(vendor_libraw PUBLIC OpenMP::OpenMP_CXX)
		endif()
		set(RAW_OPENMP "OpenMP")
	else()
		set(RAW_OPENMP "no OpenMP")
	endif()

	set(RAW_FOUND TRUE)
	set(RAW_LIBRARIES vendor_libraw)
	set(RAW_CUSTOM_LIBS vendor_libraw)
	set(RAW_INCLUDE_DIRS ${LIBRAW_SRC_DIR})
	set(RAW_LIB_TYPE "vendored LibRaw 0.22.3, ${RAW_OPENMP}; lcms2: ${LCMS2_LIB_TYPE}")
	# OpenMP on Linux: -fopenmp link flag (IMG2_INTERFACE_LINK_OPTIONS); elsewhere the OpenMP::OpenMP_CXX target
	set(_raw_system_libs "")
	if(OpenMP_CXX_FOUND AND NOT (UNIX AND NOT APPLE))
		set(_raw_system_libs OpenMP::OpenMP_CXX)
	endif()
	img2_feature(RAW LIBS ${RAW_LIBRARIES} INCLUDE_DIRS ${RAW_INCLUDE_DIRS}
		ARCHIVES "$<TARGET_FILE:vendor_libraw>" ARCHIVE_TARGETS vendor_libraw SYSTEM_LIBS ${_raw_system_libs})

elseif(IMG2_ENABLE_RAW)
	set(RAW_LIB_TYPE "host")
	pkg_check_modules(RAW libraw)

	if(RAW_FOUND)
		# libraw depends on lcms2 and other libraries
		if(UNIX AND NOT APPLE)
			# Linux: verify static libraries exist
			find_library(RAW_STATIC_LIB NAMES libraw.a libraw_r.a)
			find_library(LCMS2_STATIC_LIB NAMES liblcms2.a)
			if(NOT RAW_STATIC_LIB OR NOT LCMS2_STATIC_LIB)
				message(WARNING "libraw or liblcms2 static libraries not found (required for static linking on Linux), RAW support disabled")
				set(RAW_FOUND FALSE)
			endif()
			imgcat2_strip_vendored_ldflags(RAW_STATIC_LDFLAGS)
		elseif(APPLE)
			# macOS: explicitly find static libraries
			find_library(RAW_STATIC_LIB NAMES libraw.a libraw_r.a REQUIRED)
			find_library(LCMS2_STATIC NAMES liblcms2.a REQUIRED)
			set(RAW_CUSTOM_LIBS ${RAW_STATIC_LIB} ${LCMS2_STATIC})

			# Add optional dependencies
			find_library(JASPER_STATIC NAMES libjasper.a)
			if(JASPER_STATIC)
				list(APPEND RAW_CUSTOM_LIBS ${JASPER_STATIC})
			endif()

			# libraw on macOS (Homebrew) is compiled with OpenMP; add libomp
			find_library(OMP_LIB NAMES omp libomp
				PATHS /opt/homebrew/opt/libomp/lib /usr/local/opt/libomp/lib
				NO_DEFAULT_PATH)
			if(OMP_LIB)
				list(APPEND RAW_CUSTOM_LIBS ${OMP_LIB})
				message(STATUS "  libomp found and added for libraw: ${OMP_LIB}")
			else()
				message(WARNING "libomp not found; libraw may fail to link (OpenMP required)")
			endif()
		endif()
	endif()

	if(RAW_FOUND)
		if(APPLE)
			img2_feature(RAW LIBS ${RAW_CUSTOM_LIBS} INCLUDE_DIRS ${RAW_INCLUDE_DIRS} SYSTEM_LIBS ${RAW_CUSTOM_LIBS})
		else()
			img2_feature(RAW LIBS ${RAW_STATIC_LDFLAGS} INCLUDE_DIRS ${RAW_INCLUDE_DIRS} SYSTEM_LIBS ${RAW_STATIC_LDFLAGS})
		endif()
		img2_warn_host_dependency("libraw (IMG2_USE_SYSTEM_RAW)")

	else()
		message(WARNING "libraw not found, RAW support disabled")
		set(IMG2_ENABLE_RAW OFF)
	endif()
endif()
