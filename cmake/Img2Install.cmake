# Purpose: Install rules: public headers, generated img2_config.h, the fat libimg2.a, the relocatable
#          img2Config.cmake/img2ConfigVersion.cmake package (COMPONENT img2_development) and the imgcat2 CLI
#          (COMPONENT imgcat2).
# Inputs:  IMG2_INSTALL, IMG2_FAT_ARCHIVE, the [R16] variables of cmake/Img2FatArchive.cmake (IMG2_FAT_ARCHIVE_NAME,
#          IMG2_FAT_ARCHIVE_FILE, IMG2_NEED_OPENMP, IMG2_HOST_LINK_LIBS, IMG2_CONFIG_LINK_OPTIONS, IMG2_FEATURES),
#          IMG2_VENDOR_MANIFEST_FILE (cmake/Img2VendorManifest.cmake), target imgcat2 (if built).
# Outputs: install() rules; ${PROJECT_BINARY_DIR}/img2Config.cmake, ${PROJECT_BINARY_DIR}/img2ConfigVersion.cmake,
#          IMG2_INSTALL_ARCHIVE.

if(IMG2_INSTALL)
	if(NOT IMG2_FAT_ARCHIVE)
		message(FATAL_ERROR "img2: IMG2_INSTALL requires IMG2_FAT_ARCHIVE (the installed package is the fat libimg2.a)")
	endif()
	include(GNUInstallDirs)
	include(CMakePackageConfigHelpers)

	install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/img2" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
		COMPONENT img2_development)
	install(FILES "${PROJECT_BINARY_DIR}/include/img2/img2_config.h" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/img2
		COMPONENT img2_development)
	install(FILES "${IMG2_FAT_ARCHIVE_FILE}" DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT img2_development)
	# [F37] which vendored libraries (and commits) the archive contains
	install(FILES "${IMG2_VENDOR_MANIFEST_FILE}" DESTINATION ${CMAKE_INSTALL_DATADIR}/img2 COMPONENT img2_development)

	# [SL3] The package must not reference the build or source tree
	foreach(_img2_item IN LISTS IMG2_HOST_LINK_LIBS IMG2_CONFIG_LINK_OPTIONS)
		string(FIND "${_img2_item}" "${PROJECT_BINARY_DIR}" _img2_pos_bin)
		string(FIND "${_img2_item}" "${PROJECT_SOURCE_DIR}" _img2_pos_src)
		if(_img2_pos_bin GREATER -1 OR _img2_pos_src GREATER -1)
			message(FATAL_ERROR "img2: installed link interface would reference the build/source tree: ${_img2_item}")
		endif()
	endforeach()

	set(IMG2_INSTALL_ARCHIVE "${CMAKE_INSTALL_LIBDIR}/${IMG2_FAT_ARCHIVE_NAME}")   # relative to the prefix
	configure_package_config_file("${PROJECT_SOURCE_DIR}/cmake/img2Config.cmake.in"
		"${PROJECT_BINARY_DIR}/img2Config.cmake"
		INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/img2
		PATH_VARS CMAKE_INSTALL_INCLUDEDIR IMG2_INSTALL_ARCHIVE)
	write_basic_package_version_file("${PROJECT_BINARY_DIR}/img2ConfigVersion.cmake"
		VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
	install(FILES "${PROJECT_BINARY_DIR}/img2Config.cmake" "${PROJECT_BINARY_DIR}/img2ConfigVersion.cmake"
		DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/img2 COMPONENT img2_development)

	if(TARGET imgcat2)
		install(TARGETS imgcat2 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT imgcat2)
	endif()
endif()
