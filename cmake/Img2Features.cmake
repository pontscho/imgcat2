# Purpose: Feature contract between the vendor modules and the img2 target (feature registry, link/include order).
# Inputs:  img2_feature() / img2_register_archives() calls from cmake/vendor/*.cmake.
# Outputs: IMG2_ALL_FEATURES, IMG2_MACRO_FEATURES, IMG2_LINK_ORDER, IMG2_INCLUDE_ORDER, IMG2_FEATURE_MACRO_PREFIX,
#          IMG2_HAVE_<X>, IMG2_<X>_{LIBS,INCLUDE_DIRS,ARCHIVES}, IMG2_VENDORED_ARCHIVE_TARGETS, IMG2_SYSTEM_LINK_LIBS,
#          IMG2_TRAILING_ARCHIVES; function img2_warn_host_dependency().

# Feature names == today's macro suffix, so the S5 macro rename is one place (IMG2_FEATURE_MACRO_PREFIX).
set(IMG2_ALL_FEATURES ZLIB LIBPNG JPEGQS LIBJPEG GIFLIB WEBP HEIF TIFF RAW JXL RESVG EXIF_READER)
# Only features that had a global -DHAVE_* definition on master get a macro. ZLIB never had one.
set(IMG2_MACRO_FEATURES LIBJPEG JPEGQS LIBPNG GIFLIB WEBP HEIF TIFF RAW JXL RESVG EXIF_READER)
set(IMG2_LINK_ORDER LIBPNG ZLIB JPEGQS LIBJPEG GIFLIB WEBP HEIF TIFF RAW JXL RESVG)
# Include order is NOT the link order: PNG, ZLIB, JPEG (unconditional), then GIF, WEBP, HEIF, TIFF, RAW, JXL,
# RESVG, and jpeg-quantsmooth LAST (its directory has its own jconfig.h, which must not shadow libjpeg-turbo's).
set(IMG2_INCLUDE_ORDER LIBPNG ZLIB LIBJPEG GIFLIB WEBP HEIF TIFF RAW JXL RESVG JPEGQS)
set(IMG2_FEATURE_MACRO_PREFIX "IMG2_HAVE_")

function(img2_feature name)
	cmake_parse_arguments(F "" "" "LIBS;INCLUDE_DIRS;ARCHIVES;ARCHIVE_TARGETS;SYSTEM_LIBS" ${ARGN})
	set(IMG2_HAVE_${name} ON PARENT_SCOPE)
	set(IMG2_${name}_LIBS "${F_LIBS}" PARENT_SCOPE)
	set(IMG2_${name}_INCLUDE_DIRS "${F_INCLUDE_DIRS}" PARENT_SCOPE)
	set(IMG2_${name}_ARCHIVES "${F_ARCHIVES}" PARENT_SCOPE)   # per feature; ordered in S9 by IMG2_LINK_ORDER
	set(IMG2_VENDORED_ARCHIVE_TARGETS ${IMG2_VENDORED_ARCHIVE_TARGETS} ${F_ARCHIVE_TARGETS} PARENT_SCOPE)
	set(IMG2_SYSTEM_LINK_LIBS ${IMG2_SYSTEM_LINK_LIBS} ${F_SYSTEM_LIBS} PARENT_SCOPE)
endfunction()

# Archives of vendors that are not features (lcms2, brotli, ... from IMGCAT2_VENDORED_TRAILING_LIBS) are
# registered in trailing-lib order; S9 appends them after the feature archives.
function(img2_register_archives)
	cmake_parse_arguments(A "" "" "ARCHIVES;ARCHIVE_TARGETS" ${ARGN})
	set(IMG2_TRAILING_ARCHIVES ${IMG2_TRAILING_ARCHIVES} ${A_ARCHIVES} PARENT_SCOPE)
	set(IMG2_VENDORED_ARCHIVE_TARGETS ${IMG2_VENDORED_ARCHIVE_TARGETS} ${A_ARCHIVE_TARGETS} PARENT_SCOPE)
endfunction()

# Host libraries (IMG2_USE_SYSTEM_*, resvg) are not merged into the fat archive: they go to SYSTEM_LIBS, and the
# installed package then depends on them.
function(img2_warn_host_dependency what)
	if(IMG2_INSTALL)
		message(WARNING "img2: ${what} comes from the host; the installed img2 package depends on host libraries")
	endif()
endfunction()
