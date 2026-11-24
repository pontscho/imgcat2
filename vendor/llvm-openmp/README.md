# LLVM OpenMP runtime (libomp) 19.1.7

Committed source (not a git submodule): the host OpenMP runtime of jpeg-quantsmooth and LibRaw on macOS with
LLVM Clang 19, built as a static `libomp.a` by `cmake/vendor/Img2VendorOpenMP.cmake` (ExternalProject) and
merged into the fat `libimg2.a`. Not used on Linux (libgomp via `-fopenmp`) or with any other compiler.
The runtime version matches the compiler (Homebrew `llvm@19`, Clang 19.1.7).

License: Apache-2.0 WITH LLVM-exception (`openmp/LICENSE.TXT`; bundled ittnotify:
`openmp/runtime/src/thirdparty/ittnotify/LICENSE.txt`, compiled out in the static build).

## Source

Official LLVM 19.1.7 release, https://github.com/llvm/llvm-project/releases/tag/llvmorg-19.1.7

| Tarball | URL | sha256 |
|---|---|---|
| `openmp-19.1.7.src.tar.xz` | https://github.com/llvm/llvm-project/releases/download/llvmorg-19.1.7/openmp-19.1.7.src.tar.xz | `bd7e6901ab086fd268750363017935fd4a717c153dad3c2aab86cb0140d9e3fe` |
| `cmake-19.1.7.src.tar.xz` | https://github.com/llvm/llvm-project/releases/download/llvmorg-19.1.7/cmake-19.1.7.src.tar.xz | `11c5a28f90053b0c43d0dec3d0ad579347fc277199c005206b963c19aae514e3` |

Both `.sig` files were verified with GnuPG against https://releases.llvm.org/release-keys.asc: good signatures
by Tobias Hieta, key `D574 BD5D 1D0E 9889 5E3B F900 44F2 485E 45D5 9042`.

Layout (what the standalone openmp build of LLVM 19 expects, `openmp/../cmake`):

- `openmp/`: `openmp-19.1.7.src`
- `cmake/`: `cmake-19.1.7.src` (complete)

## Trimmed

No upstream file is modified; only directories that the configuration used here never reads were removed:

- `openmp/runtime/test/*` except `CMakeLists.txt` and `lit.site.cfg.in` (both read at configure time)
- `openmp/docs/*` except `CMakeLists.txt` (doxygen/sphinx targets, off)
- `openmp/runtime/doc/` (doxygen sources, unreferenced)
- `openmp/libompd/{src,gdb-plugin,test}` (`LIBOMP_OMPD_SUPPORT`: Linux only, OFF here)
- `openmp/tools/` (archer, multiplex: `OPENMP_ENABLE_OMPT_TOOLS` OFF)

Consequently `LIBOMP_OMPD_SUPPORT=ON` or `OPENMP_ENABLE_OMPT_TOOLS=ON` would fail to configure; restore the
directories from the tarball first.

## Updating

Download the new `openmp-*.src.tar.xz` and `cmake-*.src.tar.xz` (+ `.sig`) of the release matching the compiler,
verify the signatures, replace `openmp/` and `cmake/`, re-apply the trimming above, then update the version in
this file, `cmake/vendor/Img2VendorOpenMP.cmake` (version and the accepted Clang major) and
`cmake/Img2VendorManifest.cmake`.
