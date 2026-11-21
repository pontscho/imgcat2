# Purpose: jpeg-quantsmooth (optional JPEG decode enhancement, SIMD dispatch + OpenMP).
# Inputs:  IMG2_ENABLE_JPEGQS, JPEG_INCLUDE_DIRS.
# Outputs: jpegqs* targets, JPEGQS_FOUND, JPEGQS_OPENMP, HAVE_JPEGQS.

# jpeg-quantsmooth (optional, enhances JPEG decoding quality)
if(IMG2_ENABLE_JPEGQS AND JPEG_FOUND)
	set(JPEGQS_SRC_DIR ${PROJECT_SOURCE_DIR}/vendor/jpeg-quantsmooth)
	set(JPEGQS_SOURCE ${JPEGQS_SRC_DIR}/libjpegqs.c)
	set(JPEGQS_COMMON_FLAGS -Wall -O2 -Wno-error)

	if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
		# x86_64: 4 SIMD variants + dispatcher

		# base (helpers included: start/finish_decompress)
		add_library(jpegqs_base OBJECT ${JPEGQS_SOURCE})
		target_compile_definitions(jpegqs_base PRIVATE SIMD_NAME=base SIMD_BASE)
		target_compile_options(jpegqs_base PRIVATE ${JPEGQS_COMMON_FLAGS})
		target_include_directories(jpegqs_base PRIVATE ${JPEGQS_SRC_DIR} ${JPEG_INCLUDE_DIRS})

		# sse2
		add_library(jpegqs_sse2 OBJECT ${JPEGQS_SOURCE})
		target_compile_definitions(jpegqs_sse2 PRIVATE SIMD_NAME=sse2 SIMD_SSE2)
		target_compile_options(jpegqs_sse2 PRIVATE ${JPEGQS_COMMON_FLAGS} -msse2)
		target_include_directories(jpegqs_sse2 PRIVATE ${JPEGQS_SRC_DIR} ${JPEG_INCLUDE_DIRS})

		# avx2
		add_library(jpegqs_avx2 OBJECT ${JPEGQS_SOURCE})
		target_compile_definitions(jpegqs_avx2 PRIVATE SIMD_NAME=avx2 SIMD_AVX2)
		target_compile_options(jpegqs_avx2 PRIVATE ${JPEGQS_COMMON_FLAGS} -mavx2 -mfma)
		target_include_directories(jpegqs_avx2 PRIVATE ${JPEGQS_SRC_DIR} ${JPEG_INCLUDE_DIRS})

		# avx512
		add_library(jpegqs_avx512 OBJECT ${JPEGQS_SOURCE})
		target_compile_definitions(jpegqs_avx512 PRIVATE SIMD_NAME=avx512 SIMD_AVX512)
		target_compile_options(jpegqs_avx512 PRIVATE ${JPEGQS_COMMON_FLAGS} -mavx512f -mavx512dq -mavx512bw -mfma)
		target_include_directories(jpegqs_avx512 PRIVATE ${JPEGQS_SRC_DIR} ${JPEG_INCLUDE_DIRS})

		# dispatcher (SIMD_SELECT, CPUID-based runtime dispatch)
		add_library(jpegqs_dispatch OBJECT ${JPEGQS_SOURCE})
		target_compile_definitions(jpegqs_dispatch PRIVATE SIMD_SELECT)
		target_compile_options(jpegqs_dispatch PRIVATE ${JPEGQS_COMMON_FLAGS})
		target_include_directories(jpegqs_dispatch PRIVATE ${JPEGQS_SRC_DIR} ${JPEG_INCLUDE_DIRS})

		add_library(jpegqs STATIC
			$<TARGET_OBJECTS:jpegqs_base>
			$<TARGET_OBJECTS:jpegqs_sse2>
			$<TARGET_OBJECTS:jpegqs_avx2>
			$<TARGET_OBJECTS:jpegqs_avx512>
			$<TARGET_OBJECTS:jpegqs_dispatch>
		)

		# Disable clang-tidy for vendor OBJECT targets
		foreach(_t jpegqs_base jpegqs_sse2 jpegqs_avx2 jpegqs_avx512 jpegqs_dispatch)
			set_target_properties(${_t} PROPERTIES C_CLANG_TIDY "")
		endforeach()

	elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
		# ARM64: NEON baseline, no runtime dispatch needed
		add_library(jpegqs STATIC ${JPEGQS_SOURCE})
		target_compile_options(jpegqs PRIVATE ${JPEGQS_COMMON_FLAGS})
		target_include_directories(jpegqs PRIVATE ${JPEGQS_SRC_DIR} ${JPEG_INCLUDE_DIRS})

	else()
		# Fallback: scalar only
		add_library(jpegqs STATIC ${JPEGQS_SOURCE})
		target_compile_options(jpegqs PRIVATE ${JPEGQS_COMMON_FLAGS})
		target_include_directories(jpegqs PRIVATE ${JPEGQS_SRC_DIR} ${JPEG_INCLUDE_DIRS})
	endif()

	target_include_directories(jpegqs INTERFACE ${JPEGQS_SRC_DIR})
	set_target_properties(jpegqs PROPERTIES C_CLANG_TIDY "")

	# OpenMP: quantsmooth.h parallelizes its per-row loops under _OPENMP
	# (upstream Makefile builds with -fopenmp). Output is deterministic.
	find_package(OpenMP COMPONENTS C)
	if(OpenMP_C_FOUND)
		if(UNIX AND NOT APPLE)
			# Flag form so the gcc driver picks static libgomp under -static
			separate_arguments(_jpegqs_omp_flags UNIX_COMMAND "${OpenMP_C_FLAGS}")
			foreach(_t jpegqs jpegqs_base jpegqs_sse2 jpegqs_avx2 jpegqs_avx512 jpegqs_dispatch)
				if(TARGET ${_t})
					target_compile_options(${_t} PRIVATE ${_jpegqs_omp_flags})
				endif()
			endforeach()
			target_link_options(jpegqs INTERFACE ${_jpegqs_omp_flags})
		else()
			foreach(_t jpegqs jpegqs_base jpegqs_sse2 jpegqs_avx2 jpegqs_avx512 jpegqs_dispatch)
				if(TARGET ${_t})
					target_link_libraries(${_t} PRIVATE OpenMP::OpenMP_C)
				endif()
			endforeach()
			target_link_libraries(jpegqs PUBLIC OpenMP::OpenMP_C)
		endif()
		set(JPEGQS_OPENMP "OpenMP")
	else()
		set(JPEGQS_OPENMP "no OpenMP")
	endif()

	# Vendored libjpeg-turbo: jpeglib.h/jconfig.h only exist after its install step
	if(TARGET libjpeg-turbo-ext)
		foreach(_t jpegqs jpegqs_base jpegqs_sse2 jpegqs_avx2 jpegqs_avx512 jpegqs_dispatch)
			if(TARGET ${_t})
				add_dependencies(${_t} libjpeg-turbo-ext)
			endif()
		endforeach()
	endif()

	add_definitions(-DHAVE_JPEGQS)
	set(JPEGQS_FOUND TRUE)
	message(STATUS "jpeg-quantsmooth: enabled (quality=6, niter=5, ${JPEGQS_OPENMP})")
endif()
