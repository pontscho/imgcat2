# Purpose: Merge libimg2.a and every vendored archive into one fat libimg2.a (build/fat/), plus the link
#          interface (system libraries, link options) shared by the in-tree img2_bundled target and the
#          installed img2Config.cmake.
# Inputs:  IMG2_LINK_ORDER, IMG2_HAVE_<X>, IMG2_<X>_ARCHIVES, IMG2_TRAILING_ARCHIVES, IMG2_VENDORED_ARCHIVE_TARGETS,
#          IMG2_SYSTEM_LINK_LIBS, IMG2_ALL_FEATURES, JPEGQS_OPENMP, RAW_OPENMP, IMG2_OPENMP_VENDORED, OpenMP_<L>_FLAGS,
#          IMG2_POSITION_INDEPENDENT_CODE, IMG2_FAT_ARCHIVE, STATIC_LIBCXX_LIBS/DYNAMIC_LIBCXX_LIBS, Threads::Threads.
# Outputs: macro img2_add_fat_archive(); after the call ([R16] variable contract, docs/feature-implementation-plan.md):
#          IMG2_VENDORED_ARCHIVES, IMG2_FAT_ARCHIVE_NAME, IMG2_FAT_ARCHIVE_FILE, IMG2_SYSTEM_LINK_LIBS (completed),
#          IMG2_NEED_OPENMP, IMG2_HOST_LINK_LIBS, IMG2_INTERFACE_LINK_OPTIONS, IMG2_CONFIG_LINK_OPTIONS,
#          IMG2_FEATURES, IMG2_FAT_LINK_LANGUAGES; target img2_fat (when IMG2_FAT_ARCHIVE).
#
# Duplicate symbols: no strong global symbol is defined in two of the merged archives, so the ADDLIB order
# below (== the former link order) needs no reordering; within one archive the linker rescans until nothing new
# resolves. Enforced at build time since img2/s12b: cmake/scripts/img2_check_duplicate_symbols.cmake runs on
# every merged archive (allow-list: cmake/scripts/img2_duplicate_symbols_allowlist.txt).
#
# Vendored-library manifest ([F37]): cmake/Img2VendorManifest.cmake writes ${IMG2_VENDOR_MANIFEST_FILE}
# (build/fat/vendored-libraries.txt: submodule path, URL, pinned gitlink commit, checked-out commit, git describe)
# at configure time (top-level CMakeLists.txt, right after img2_add_fat_archive()); cmake/Img2Install.cmake
# installs it as share/img2/vendored-libraries.txt.
#
# A macro (not a function), so every variable lands in the caller's (top-level) scope.
macro(img2_add_fat_archive)
	# [R8] Merge order == the former link order: IMG2_LINK_ORDER walk, then the trailing providers
	set(IMG2_VENDORED_ARCHIVES "")
	foreach(_f IN LISTS IMG2_LINK_ORDER)
		if(IMG2_HAVE_${_f})
			list(APPEND IMG2_VENDORED_ARCHIVES ${IMG2_${_f}_ARCHIVES})
		endif()
	endforeach()
	list(APPEND IMG2_VENDORED_ARCHIVES ${IMG2_TRAILING_ARCHIVES})

	# OpenMP (jpeg-quantsmooth, vendored LibRaw): does the consumer need an OpenMP runtime of its own? Not with the
	# vendored static libomp (macOS/Clang, cmake/vendor/Img2VendorOpenMP.cmake): libomp.a is merged into the fat
	# archive, and a second runtime in the process would abort (OMP Error #15)
	if(IMG2_OPENMP_VENDORED)
		set(IMG2_NEED_OPENMP 0)
	elseif(JPEGQS_OPENMP STREQUAL "OpenMP" OR RAW_OPENMP STREQUAL "OpenMP")
		set(IMG2_NEED_OPENMP 1)
	else()
		set(IMG2_NEED_OPENMP 0)
	endif()

	# Link options of the in-tree bundled target: the -fopenmp flag form on Linux (static libgomp under -static,
	# as the vendor targets' INTERFACE link options did), -no-pie when the library is not PIC
	set(IMG2_INTERFACE_LINK_OPTIONS "")
	set(IMG2_CONFIG_LINK_OPTIONS "")
	if(IMG2_NEED_OPENMP AND UNIX AND NOT APPLE)
		if(JPEGQS_OPENMP STREQUAL "OpenMP")
			separate_arguments(_img2_omp_flags UNIX_COMMAND "${OpenMP_C_FLAGS}")
		else()
			separate_arguments(_img2_omp_flags UNIX_COMMAND "${OpenMP_CXX_FLAGS}")
		endif()
		list(APPEND IMG2_INTERFACE_LINK_OPTIONS ${_img2_omp_flags})
	endif()
	if(NOT IMG2_POSITION_INDEPENDENT_CODE)
		list(APPEND IMG2_INTERFACE_LINK_OPTIONS "$<$<PLATFORM_ID:Linux>:-no-pie>")
		list(APPEND IMG2_CONFIG_LINK_OPTIONS "$<$<PLATFORM_ID:Linux>:-no-pie>")
	endif()

	# System libraries: host libraries from the img2_feature(... SYSTEM_LIBS ...) calls, then Threads, m and the
	# C++ runtime (vendored LibRaw, libheif, libjxl and highway are C++)
	list(APPEND IMG2_SYSTEM_LINK_LIBS Threads::Threads)
	if(UNIX AND NOT APPLE)
		list(APPEND IMG2_SYSTEM_LINK_LIBS m)
	endif()
	if(CMAKE_CXX_COMPILER_LOADED)
		set(IMG2_FAT_LINK_LANGUAGES C CXX)
		if(APPLE AND STATIC_LIBCXX_LIBS)
			list(APPEND IMG2_SYSTEM_LINK_LIBS ${STATIC_LIBCXX_LIBS})
		elseif(APPLE)
			list(APPEND IMG2_SYSTEM_LINK_LIBS c++)
		else()
			list(APPEND IMG2_SYSTEM_LINK_LIBS stdc++)
		endif()
	else()
		set(IMG2_FAT_LINK_LANGUAGES C)
	endif()

	# Host libraries only, for the installed config (it re-adds Threads/OpenMP/m/stdc++ portably); imported
	# targets are flattened to their library file
	set(IMG2_HOST_LINK_LIBS "")
	foreach(_l IN LISTS IMG2_SYSTEM_LINK_LIBS)
		if(_l STREQUAL "Threads::Threads" OR _l MATCHES "^OpenMP::" OR _l MATCHES "^(m|stdc\\+\\+|c\\+\\+)$"
				OR _l MATCHES "^-l(m|stdc\\+\\+|c\\+\\+|pthread)$" OR _l IN_LIST STATIC_LIBCXX_LIBS)
			continue()
		endif()
		if(TARGET ${_l})
			get_target_property(_img2_loc ${_l} IMPORTED_LOCATION)
			if(_img2_loc)
				list(APPEND IMG2_HOST_LINK_LIBS "${_img2_loc}")
			else()
				message(WARNING "img2: cannot flatten host link target '${_l}' for the installed package")
			endif()
		else()
			list(APPEND IMG2_HOST_LINK_LIBS "${_l}")
		endif()
	endforeach()

	set(IMG2_FEATURES "")
	foreach(_f IN LISTS IMG2_ALL_FEATURES)
		if(IMG2_HAVE_${_f})
			list(APPEND IMG2_FEATURES ${_f})
		endif()
	endforeach()

	# [R16]
	set(IMG2_FAT_ARCHIVE_NAME "${CMAKE_STATIC_LIBRARY_PREFIX}img2${CMAKE_STATIC_LIBRARY_SUFFIX}")
	set(IMG2_FAT_ARCHIVE_FILE "${PROJECT_BINARY_DIR}/fat/${IMG2_FAT_ARCHIVE_NAME}")

	if(IMG2_FAT_ARCHIVE)
		# $<TARGET_FILE:img2> / DEPENDS img2 name img2 before it exists: both resolve at generate time
		file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/fat/inputs.txt"
			CONTENT "$<TARGET_FILE:img2>\n$<JOIN:${IMG2_VENDORED_ARCHIVES},\n>\n")   # [R8] order
		# [F37] after the merge: no strong global symbol may be defined by two members (GNU/LLVM nm; skipped on
		# Apple and without nm). The check deletes the archive on failure, so the next build re-runs both steps.
		set(_img2_dupcheck_cmd "")
		set(_img2_dupcheck_deps "")
		if(CMAKE_NM AND NOT APPLE)
			set(_img2_dupcheck_cmd COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} -DARCHIVE=${IMG2_FAT_ARCHIVE_FILE}
				-DALLOWLIST=${PROJECT_SOURCE_DIR}/cmake/scripts/img2_duplicate_symbols_allowlist.txt
				-P ${PROJECT_SOURCE_DIR}/cmake/scripts/img2_check_duplicate_symbols.cmake)
			set(_img2_dupcheck_deps "${PROJECT_SOURCE_DIR}/cmake/scripts/img2_check_duplicate_symbols.cmake"
				"${PROJECT_SOURCE_DIR}/cmake/scripts/img2_duplicate_symbols_allowlist.txt")
		endif()
		add_custom_command(OUTPUT "${IMG2_FAT_ARCHIVE_FILE}"
			COMMAND ${CMAKE_COMMAND} -DMODE=$<IF:$<PLATFORM_ID:Darwin>,libtool,mri> -DAR=${CMAKE_AR}
			        -DRANLIB=${CMAKE_RANLIB} -DINPUTS=${PROJECT_BINARY_DIR}/fat/inputs.txt
			        -DOUTPUT=${IMG2_FAT_ARCHIVE_FILE}
			        -P ${PROJECT_SOURCE_DIR}/cmake/scripts/img2_fat_archive.cmake
			${_img2_dupcheck_cmd}
			DEPENDS img2 ${IMG2_VENDORED_ARCHIVE_TARGETS} ${IMG2_VENDORED_ARCHIVES}
			        "${PROJECT_BINARY_DIR}/fat/inputs.txt" "${PROJECT_SOURCE_DIR}/cmake/scripts/img2_fat_archive.cmake"
			        ${_img2_dupcheck_deps}
			COMMENT "Merging img2 and the vendored archives into ${IMG2_FAT_ARCHIVE_NAME}"
			VERBATIM)
		add_custom_target(img2_fat ALL DEPENDS "${IMG2_FAT_ARCHIVE_FILE}")
	endif()
endmacro()
