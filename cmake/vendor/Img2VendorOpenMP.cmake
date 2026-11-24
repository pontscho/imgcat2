# Purpose: OpenMP runtime of jpeg-quantsmooth and the vendored LibRaw on macOS + Clang: static libomp built from
#          the committed LLVM 19.1.7 sources (vendor/llvm-openmp, see its README.md) via ExternalProject.
# Inputs:  APPLE, CMAKE_C_COMPILER_ID/VERSION, IMG2_USE_SYSTEM_OPENMP, IMG2_ENABLE_JPEGQS, IMG2_ENABLE_RAW,
#          IMG2_USE_SYSTEM_RAW, IMG2_SANITIZE, IMG2_FUZZ, IMG2_HARDENING_FLAGS_STRING, IMG2_POSITION_INDEPENDENT_CODE.
# Outputs: libomp-ext (ExternalProject), vendor_libomp (IMPORTED static library: libomp.a, omp.h include dir,
#          -fopenmp=libomp compile option; the OpenMP::OpenMP_<L> equivalent), IMG2_OPENMP_VENDORED,
#          LIBOMP_LIBRARY, LIBOMP_LIB_TYPE; img2_register_archives(libomp.a) for the fat archive.
#
# Why: the static third-party policy (CMAKE_FIND_LIBRARY_SUFFIXES .a, cmake/Img2Platform.cmake) makes FindOpenMP
# fail on macOS, where Homebrew LLVM only ships libomp.dylib, so jpeg-quantsmooth and LibRaw ran single-threaded.
# The runtime version follows the compiler (LLVM 19): any other Clang major, AppleClang, IMG2_USE_SYSTEM_OPENMP=ON
# and the sanitizer / fuzz configurations keep the previous FindOpenMP lookup (Img2VendorJpegqs.cmake,
# Img2VendorRaw.cmake). Linux is never affected (-fopenmp, libgomp).
# ExternalProject (not add_subdirectory): the standalone openmp build is a project of its own (project(), global
# CMAKE_CXX_STANDARD cache entry, its own option()/cache set, lit/FileCheck probes) that must not land in our
# cache or a consumer's, and our -Werror / vendor-section flags must not reach it. The install step only fills
# ${PROJECT_BINARY_DIR}/_deps/libomp-install; nothing of it is part of img2's install (libomp.a is merged into
# the fat libimg2.a instead).

set(IMG2_OPENMP_VENDORED FALSE)
if(APPLE AND NOT IMG2_USE_SYSTEM_OPENMP AND NOT IMG2_SANITIZE AND NOT IMG2_FUZZ
		AND (IMG2_ENABLE_JPEGQS OR (IMG2_ENABLE_RAW AND NOT IMG2_USE_SYSTEM_RAW)))
	set(_libomp_version 19.1.7)
	if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
		message(STATUS "libomp: vendored ${_libomp_version} not used (C compiler is ${CMAKE_C_COMPILER_ID}, needs LLVM Clang 19)")
	elseif(NOT CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 19 OR CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 20)
		message(STATUS "libomp: vendored ${_libomp_version} not used (Clang ${CMAKE_C_COMPILER_VERSION}; the runtime must match the compiler, LLVM 19)")
	else()
		# The C++ compiler of the runtime build; CXX itself is enabled later (LibRaw, libheif, libjxl)
		include(CheckLanguage)
		check_language(CXX)
		find_package(Python3 REQUIRED COMPONENTS Interpreter)   # libomp's message catalogue generator
		if(NOT CMAKE_CXX_COMPILER)
			message(STATUS "libomp: vendored ${_libomp_version} not used (no C++ compiler)")
		else()
			set(IMG2_OPENMP_VENDORED TRUE)
		endif()
	endif()
endif()

if(IMG2_OPENMP_VENDORED)
	include(ExternalProject)
	set(LIBOMP_PREFIX "${PROJECT_BINARY_DIR}/_deps/libomp-install")
	set(LIBOMP_LIBRARY "${LIBOMP_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}omp${CMAKE_STATIC_LIBRARY_SUFFIX}")
	set(_libomp_cache_args
		-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}
		-DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}
		-DCMAKE_BUILD_TYPE:STRING=Release
		-DCMAKE_INSTALL_PREFIX:PATH=${LIBOMP_PREFIX}
		-DCMAKE_INSTALL_INCLUDEDIR:PATH=include
		-DOPENMP_INSTALL_LIBDIR:PATH=lib
		-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=${IMG2_POSITION_INDEPENDENT_CODE}
		-DPython3_EXECUTABLE:FILEPATH=${Python3_EXECUTABLE}
		# Host runtime only, static, no aliases (libgomp.a / libiomp5.a), no tools / OMPT / OMPD / offloading
		-DLIBOMP_ENABLE_SHARED:BOOL=OFF
		-DLIBOMP_INSTALL_ALIASES:BOOL=OFF
		-DLIBOMP_ENABLE_ASSERTIONS:BOOL=OFF          # as LLVM release builds (LLVM_ENABLE_ASSERTIONS OFF)
		-DLIBOMP_OMPT_SUPPORT:BOOL=OFF
		-DLIBOMP_OMPD_SUPPORT:BOOL=OFF
		-DLIBOMP_USE_HWLOC:BOOL=OFF
		-DLIBOMP_FORTRAN_MODULES:BOOL=OFF
		-DLIBOMP_COPY_EXPORTS:BOOL=OFF              # never write into the source tree
		-DOPENMP_ENABLE_LIBOMPTARGET:BOOL=OFF
		-DOPENMP_ENABLE_OMPT_TOOLS:BOOL=OFF
		-DOPENMP_ENABLE_LIBOMP_PROFILING:BOOL=OFF
		-DOPENMP_ENABLE_WERROR:BOOL=OFF
	)
	if(IMG2_HARDENING_FLAGS_STRING)
		# [F32] hardening flags of the vendor section (C/C++), as for libjpeg-turbo and aom
		list(APPEND _libomp_cache_args
			"-DCMAKE_C_FLAGS:STRING=${IMG2_HARDENING_FLAGS_STRING}"
			"-DCMAKE_CXX_FLAGS:STRING=${IMG2_HARDENING_FLAGS_STRING}")
	endif()
	list(APPEND _libomp_cache_args
		"-DCMAKE_OSX_ARCHITECTURES:STRING=${CMAKE_OSX_ARCHITECTURES}"
		"-DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=${CMAKE_OSX_DEPLOYMENT_TARGET}"
		"-DCMAKE_OSX_SYSROOT:PATH=${CMAKE_OSX_SYSROOT}")
	ExternalProject_Add(libomp-ext
		SOURCE_DIR "${PROJECT_SOURCE_DIR}/vendor/llvm-openmp/openmp"
		BINARY_DIR "${PROJECT_BINARY_DIR}/_deps/libomp-build"
		INSTALL_DIR "${LIBOMP_PREFIX}"
		CMAKE_CACHE_ARGS ${_libomp_cache_args}
		# The message catalogue generator (runtime/tools/message-converter.py) imports libomputils: no
		# __pycache__ in the source tree. --config $<CONFIG> as ExternalProject's default build command.
		BUILD_COMMAND ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
			${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG>
		BUILD_BYPRODUCTS "${LIBOMP_LIBRARY}"
		UPDATE_COMMAND ""
		EXCLUDE_FROM_ALL TRUE
	)
	# omp.h is installed at build time; the directory must exist at generate time
	file(MAKE_DIRECTORY "${LIBOMP_PREFIX}/include")
	# OpenMP::OpenMP_<L> equivalent. omp.h: the -I directory below precedes clang's resource directory (which
	# Homebrew LLVM ships with its own omp.h of the same 19.1.7 release), so the vendored header wins.
	# Compile option only: -fopenmp at link time would make the driver add -lomp (the dylib); the static
	# libomp.a is linked as this target's IMPORTED_LOCATION instead.
	add_library(vendor_libomp STATIC IMPORTED GLOBAL)
	set_target_properties(vendor_libomp PROPERTIES
		IMPORTED_LOCATION "${LIBOMP_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${LIBOMP_PREFIX}/include"
		INTERFACE_COMPILE_OPTIONS "$<$<COMPILE_LANGUAGE:C,CXX>:-fopenmp=libomp>")
	add_dependencies(vendor_libomp libomp-ext)
	# Fat archive: libomp.a is merged into libimg2.a, so the installed package needs no OpenMP of its own
	# (cmake/Img2FatArchive.cmake: IMG2_NEED_OPENMP 0)
	img2_register_archives(ARCHIVES "${LIBOMP_LIBRARY}" ARCHIVE_TARGETS libomp-ext)
	set(LIBOMP_LIB_TYPE "vendored libomp ${_libomp_version}, static")
	message(STATUS "libomp: ${LIBOMP_LIB_TYPE} (vendor/llvm-openmp, Clang ${CMAKE_C_COMPILER_VERSION})")
endif()
