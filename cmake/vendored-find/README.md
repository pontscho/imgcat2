# Find-module shims for vendored dependencies

The vendored libheif and libjxl call `find_package()` for their codec/back-end
libraries (libde265, kvazaar, aom, OpenJPEG, Brotli, libsharpyuv, JPEG, Highway,
lcms2). Their own find modules search the host (pkg-config, `find_library`,
CMake package configs), which would mix host libraries into a vendored build.

The top-level `CMakeLists.txt` prepends this directory to `CMAKE_MODULE_PATH`
only while adding those two subprojects. Each shim reports the dependency as
found **only** when the top-level declared the vendored provider through
`IMGCAT2_VENDORED_<NAME>_LIBRARIES` / `IMGCAT2_VENDORED_<NAME>_INCLUDE_DIRS`;
otherwise it reports "not found" (no host fallback), except `FindJPEG.cmake`,
which defers to CMake's own module when libjpeg-turbo comes from the host
(`USE_SYSTEM_JPEG=ON`).

The variables each shim sets are the ones the consuming upstream CMake reads
(libheif: `<X>_FOUND`, `<X>_LIBRARIES`, `<X>_INCLUDE_DIRS`; libjxl: the targets
plus `<X>_FOUND`).
