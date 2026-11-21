# Purpose: Little-CMS 2: color management of the vendored LibRaw and libjxl.
# Inputs:  IMG2_ENABLE_RAW, IMG2_ENABLE_JXL, IMG2_USE_SYSTEM_{LCMS2,RAW,JXL}.
# Outputs: vendor_lcms2 / vendor_lcms2_host, LCMS2_TARGET, LCMS2_LIB_TYPE.

# Little-CMS 2 (MIT): color management of the vendored LibRaw and libjxl (JPEGXL_ENABLE_SKCMS=OFF)
if((IMG2_ENABLE_RAW AND NOT IMG2_USE_SYSTEM_RAW) OR (IMG2_ENABLE_JXL AND NOT IMG2_USE_SYSTEM_JXL))
	if(IMG2_USE_SYSTEM_LCMS2)
		pkg_check_modules(LCMS2 REQUIRED lcms2)
		find_library(LCMS2_STATIC_LIB NAMES liblcms2.a lcms2 REQUIRED)
		add_library(vendor_lcms2_host INTERFACE)
		target_include_directories(vendor_lcms2_host INTERFACE ${LCMS2_INCLUDE_DIRS})
		target_link_libraries(vendor_lcms2_host INTERFACE ${LCMS2_STATIC_LIB} m)
		set(LCMS2_TARGET vendor_lcms2_host)
		set(LCMS2_LIB_TYPE "host")
	else()
		# Pattern B: hand-written static target from src/*.c
		set(LCMS2_SRC_DIR "${PROJECT_SOURCE_DIR}/vendor/lcms2")
		file(GLOB LCMS2_SOURCES "${LCMS2_SRC_DIR}/src/*.c")
		add_library(vendor_lcms2 STATIC ${LCMS2_SOURCES})
		target_include_directories(vendor_lcms2
			PUBLIC ${LCMS2_SRC_DIR}/include
			PRIVATE ${LCMS2_SRC_DIR}/src)
		set_target_properties(vendor_lcms2 PROPERTIES
			POSITION_INDEPENDENT_CODE ON
			C_CLANG_TIDY "")
		if(UNIX)
			target_link_libraries(vendor_lcms2 PUBLIC m)
		endif()
		set(LCMS2_TARGET vendor_lcms2)
		set(LCMS2_LIB_TYPE "vendored lcms2 2.20")
	endif()
endif()
