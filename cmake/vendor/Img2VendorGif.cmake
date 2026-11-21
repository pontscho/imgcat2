# Purpose: giflib (optional): vendored or host.
# Inputs:  IMG2_ENABLE_GIF, IMG2_USE_SYSTEM_GIF.
# Outputs: vendor_giflib, GIF_FOUND, GIF_LIBRARY, GIF_INCLUDE_DIR, GIF_LIB_TYPE, HAVE_GIFLIB.

# giflib (optional)
if(IMG2_ENABLE_GIF)
	if(IMG2_USE_SYSTEM_GIF)
		find_library(GIF_LIBRARY NAMES gif)
		find_path(GIF_INCLUDE_DIR gif_lib.h)
		set(GIF_LIB_TYPE "host")

	else()
		# Vendored giflib: hand-written static target (upstream only ships a Makefile).
		# Source list mirrors SOURCES in vendor/giflib/Makefile (libgif, no utilities).
		set(GIFLIB_SRC_DIR "${PROJECT_SOURCE_DIR}/vendor/giflib")
		add_library(vendor_giflib STATIC
			${GIFLIB_SRC_DIR}/dgif_lib.c
			${GIFLIB_SRC_DIR}/egif_lib.c
			${GIFLIB_SRC_DIR}/gifalloc.c
			${GIFLIB_SRC_DIR}/gif_err.c
			${GIFLIB_SRC_DIR}/gif_font.c
			${GIFLIB_SRC_DIR}/gif_hash.c
			${GIFLIB_SRC_DIR}/openbsd-reallocarray.c
			${GIFLIB_SRC_DIR}/quantize.c
		)
		target_include_directories(vendor_giflib PUBLIC ${GIFLIB_SRC_DIR})
		set_target_properties(vendor_giflib PROPERTIES
			C_STANDARD 99
			C_EXTENSIONS ON
			POSITION_INDEPENDENT_CODE ON
			C_CLANG_TIDY "")
		set(GIF_LIBRARY vendor_giflib)
		set(GIF_INCLUDE_DIR ${GIFLIB_SRC_DIR})
		set(GIF_LIB_TYPE "vendored giflib 6.1.3")
	endif()

	if(GIF_LIBRARY AND GIF_INCLUDE_DIR)
		add_definitions(-DHAVE_GIFLIB)
		set(GIF_FOUND TRUE)

	else()
		message(WARNING "giflib not found, GIF support disabled")
		set(GIF_FOUND FALSE)
		set(IMG2_ENABLE_GIF OFF)
	endif()
endif()
