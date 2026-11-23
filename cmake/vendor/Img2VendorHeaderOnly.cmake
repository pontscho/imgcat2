# Purpose: Header-only vendors: stb, qoi.
# Inputs:  vendor/{stb,qoi}.
# Outputs: STB_INCLUDE_DIR, QOI_INCLUDE_DIR (img2 adds them to its own include list).
# nanosvg is not taken from vendor/: img2 compiles its fork in src/libimg2/decoders (nanosvg.h, nanosvgrast.h,
# found next to decoder_nanosvg.c); vendor/nanosvg is only the upstream baseline (docs/nanosvg-fork.md).

# STB headers (vendored)
set(STB_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/vendor/stb)

# QOI headers (vendored, header-only)
set(QOI_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/vendor/qoi)
