#!/usr/bin/env bash
# Source-based coverage of libimg2 (clang -fprofile-instr-generate -fcoverage-mapping, llvm-cov).
#
#   scripts/coverage.sh [--no-build] [--no-test] [--no-check]
#
# Environment (all optional):
#   BUILD_DIR   coverage build tree             (default: <repo>/build-cov)
#   OUT_DIR     reports                         (default: $BUILD_DIR/coverage)
#   JOBS        build parallelism               (default: 16)
#   CTEST_JOBS  ctest parallelism               (default: 8)
#   CC / CXX    clang drivers                   (default: clang / clang++)
#   LLVM_PROFDATA / LLVM_COV                    (default: llvm-profdata[-17] / llvm-cov[-17], first found)
#   TMPDIR      honoured by the build and the tests (default: $BUILD_DIR/tmp)
#
# Steps: configure $BUILD_DIR with clang and IMG2_COVERAGE=ON (only libimg2's own sources are instrumented;
# IMG2_WERROR=OFF: the sources are -Werror clean with the project's GCC, clang 17 adds warnings such as
# -Wunknown-warning-option for GCC-only pragmas; the hardening flags stay on),
# build, run every ctest test except the labels "guard" (nm/objdump checks of the archive; the instrumented
# objects carry profile counters) and "consumer" (they rebuild/install img2 in their own trees and contribute
# nothing to this build's profile), each process writing $OUT_DIR/profraw/%p-%m.profraw, merge, then write
#   $OUT_DIR/report.txt       llvm-cov report (lines, regions, functions, branches) per file
#   $OUT_DIR/show.txt         annotated sources (uncovered lines / regions / branches)
#   $OUT_DIR/html/index.html  the same as HTML
#   $OUT_DIR/summary.json     llvm-cov export -summary-only
#   $OUT_DIR/export.json      llvm-cov export (full JSON: functions, regions, branches)
#   $OUT_DIR/lcov.info        llvm-cov export -format=lcov (per-line counts)
# for src/libimg2 only (vendor/, src/tests/, src/imgcat2/ and the build trees are ignored), then run
# scripts/coverage_check.py against docs/coverage-exclusions.txt (skipped with --no-check): it fails when
# anything uncovered is not in the manifest, a manifest entry is covered (stale) or an anchor no longer matches.
# Exit status: non-zero if the build fails, any test fails (the reports are still written after test failures)
# or the exclusion check fails.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-cov}"
OUT_DIR="${OUT_DIR:-$BUILD_DIR/coverage}"
JOBS="${JOBS:-16}"
CTEST_JOBS="${CTEST_JOBS:-8}"
CC="${CC:-clang}"
CXX="${CXX:-clang++}"
export TMPDIR="${TMPDIR:-$BUILD_DIR/tmp}"
mkdir -p "$TMPDIR"

pick() { for t in "$@"; do if command -v "$t" >/dev/null 2>&1; then echo "$t"; return 0; fi; done; return 1; }
LLVM_PROFDATA="${LLVM_PROFDATA:-$(pick llvm-profdata llvm-profdata-17 || true)}"
LLVM_COV="${LLVM_COV:-$(pick llvm-cov llvm-cov-17 || true)}"
[ -n "$LLVM_PROFDATA" ] || { echo "coverage.sh: llvm-profdata not found (set LLVM_PROFDATA)" >&2; exit 2; }
[ -n "$LLVM_COV" ] || { echo "coverage.sh: llvm-cov not found (set LLVM_COV)" >&2; exit 2; }

do_build=1
do_test=1
do_check=1
for a in "$@"; do
	case "$a" in
		--no-build) do_build=0 ;;
		--no-test) do_test=0 ;;
		--no-check) do_check=0 ;;
		*) echo "usage: $0 [--no-build] [--no-test] [--no-check]" >&2; exit 2 ;;
	esac
done

if [ "$do_build" = 1 ]; then
	cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" -DIMG2_COVERAGE=ON -DIMG2_WERROR=OFF
	cmake --build "$BUILD_DIR" -j"$JOBS"
fi

PROFRAW_DIR="$OUT_DIR/profraw"
test_rc=0
if [ "$do_test" = 1 ]; then
	rm -rf "$PROFRAW_DIR"
	mkdir -p "$PROFRAW_DIR"
	LLVM_PROFILE_FILE="$PROFRAW_DIR/%p-%m.profraw" \
		ctest --test-dir "$BUILD_DIR" -j"$CTEST_JOBS" --output-on-failure -LE 'guard|consumer' || test_rc=$?
fi

shopt -s nullglob
raws=("$PROFRAW_DIR"/*.profraw)
if [ "${#raws[@]}" = 0 ]; then
	echo "coverage.sh: no .profraw files in $PROFRAW_DIR" >&2
	exit 1
fi
"$LLVM_PROFDATA" merge -sparse "${raws[@]}" -o "$OUT_DIR/merged.profdata"

# Every instrumented executable: the tests (src/tests) and the CLI (if built)
objs=()
for f in "$BUILD_DIR"/src/tests/* "$BUILD_DIR"/imgcat2; do
	if [ -f "$f" ] && [ -x "$f" ]; then
		if [ "${#objs[@]}" = 0 ]; then objs+=("$f"); else objs+=("-object" "$f"); fi
	fi
done

# Not measured: vendored code, tests, the CLI, build trees, the generated font table, and the third-party
# nanosvg headers copied into src/libimg2/decoders (nanosvg.h, nanosvgrast.h; see docs/coverage.md)
IGNORE='(^|/)(vendor|src/tests|src/imgcat2|build[^/]*)/|embedded_fonts\.c$|/decoders/nanosvg(rast)?\.h$'
common=(-instr-profile "$OUT_DIR/merged.profdata" -ignore-filename-regex "$IGNORE")

"$LLVM_COV" report "${objs[@]}" "${common[@]}" -show-region-summary -show-branch-summary \
	"$ROOT/src/libimg2" | tee "$OUT_DIR/report.txt"
"$LLVM_COV" show "${objs[@]}" "${common[@]}" -show-branches=count -show-line-counts-or-regions \
	-show-expansions "$ROOT/src/libimg2" > "$OUT_DIR/show.txt"
"$LLVM_COV" show "${objs[@]}" "${common[@]}" -show-branches=count -show-line-counts-or-regions \
	-format=html -output-dir="$OUT_DIR/html" "$ROOT/src/libimg2"
"$LLVM_COV" export "${objs[@]}" "${common[@]}" -summary-only "$ROOT/src/libimg2" > "$OUT_DIR/summary.json"
"$LLVM_COV" export "${objs[@]}" "${common[@]}" -format=text "$ROOT/src/libimg2" > "$OUT_DIR/export.json"
"$LLVM_COV" export "${objs[@]}" "${common[@]}" -format=lcov "$ROOT/src/libimg2" > "$OUT_DIR/lcov.info"

echo "coverage.sh: reports in $OUT_DIR (report.txt, show.txt, html/index.html, summary.json, export.json, lcov.info)"

check_rc=0
if [ "$do_check" = 1 ]; then
	python3 -I "$ROOT/scripts/coverage_check.py" --root "$ROOT" --json "$OUT_DIR/export.json" \
		--lcov "$OUT_DIR/lcov.info" --manifest "$ROOT/docs/coverage-exclusions.txt" || check_rc=$?
fi
[ "$test_rc" = 0 ] || exit "$test_rc"
exit "$check_rc"
