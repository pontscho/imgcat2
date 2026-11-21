# Purpose: Header-only vendors: nanosvg, stb, qoi.
# Inputs:  vendor/{nanosvg,stb,qoi}.
# Outputs: NANOSVG_HEADER, NANOSVG_INCLUDE_DIR, STB_INCLUDE_DIR, QOI_INCLUDE_DIR (img2 adds them to its own include list).

# NanoSVG (optional, header-only library in vendor/)
# Check if nanosvg submodule exists
set(NANOSVG_HEADER ${PROJECT_SOURCE_DIR}/vendor/nanosvg/src/nanosvg.h)
if(EXISTS ${NANOSVG_HEADER})
else()
	message(FATAL "nanosvg not found in vendor/nanosvg/src/, SVG support disabled")
	message(FATAL "  Run: git submodule update --init --recursive")
endif()

# STB headers (vendored)
set(STB_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/vendor/stb)

# QOI headers (vendored, header-only)
set(QOI_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/vendor/qoi)

# NanoSVG headers (vendored, header-only)
set(NANOSVG_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/vendor/nanosvg/src)
