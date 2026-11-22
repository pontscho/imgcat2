#!/usr/bin/env bash
# Sanitizer builds of libimg2 + the vendored codecs + tests + imgcat2 (CMake option IMG2_SANITIZE).
#
#   scripts/sanitize.sh asan|tsan [--no-build] [--no-test] [--cli <file-or-dir>...]
#
#   asan  -DIMG2_SANITIZE=address,undefined  (ASan + LSan + UBSan, every UBSan check fatal)
#   tsan  -DIMG2_SANITIZE=thread
#
# Environment (all optional):
#   BUILD_DIR   build tree        (default: <repo>/build-asan or <repo>/build-tsan)
#   OUT_DIR     CLI sweep outputs and sanitizer logs (default: $BUILD_DIR/sanitize)
#   JOBS        build parallelism (default: 16)
#   CTEST_JOBS  ctest parallelism (default: 8)
#   CLI_JOBS    files swept in parallel by --cli (default: $CTEST_JOBS); CLI_TIMEOUT per run in s (default: 900)
#   CC / CXX    compiler drivers  (default: clang / clang++)
#   TMPDIR      honoured by the build and the tests (default: $BUILD_DIR/tmp)
#   ASAN_OPTIONS / UBSAN_OPTIONS / TSAN_OPTIONS override the defaults below.
#
# Steps: configure + build (IMG2_WERROR=OFF: clang 17 adds warnings the project's GCC does not have), then
# ctest without the labels "guard" (nm/objdump checks of the archive reject the sanitizer runtime references
# by design), "consumer" (they install / rebuild img2 in their own trees without the sanitizer flags) and
# "expensive", and without "no-sanitize" (test_cov_c_alloc: crash-probing forked children and mallinfo2() leak
# counting cannot work under the sanitizer runtimes; it runs in the normal build). With --cli, imgcat2
# runs over every regular file of the given files / directories with typical option sets (ANSI, kitty and
# iTerm2 rendering, --info --json, conversion to every encoder format into $OUT_DIR/out/<n>/, a short -a loop);
# every sanitizer report goes to $OUT_DIR/logs/ (log_path) and fails the run, as do signal exits and timeouts.
# See docs/sanitizers.md.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-}"
case "$mode" in
	asan) san="address,undefined" ;;
	tsan) san="thread" ;;
	*) echo "usage: $0 asan|tsan [--no-build] [--no-test] [--cli <file-or-dir>...]" >&2; exit 2 ;;
esac
shift
BUILD_DIR="${BUILD_DIR:-$ROOT/build-$mode}"
OUT_DIR="${OUT_DIR:-$BUILD_DIR/sanitize}"
JOBS="${JOBS:-16}"
CTEST_JOBS="${CTEST_JOBS:-8}"
CC="${CC:-clang}"
CXX="${CXX:-clang++}"
export TMPDIR="${TMPDIR:-$BUILD_DIR/tmp}"
mkdir -p "$TMPDIR"

do_build=1
do_test=1
cli_inputs=()
while [ $# -gt 0 ]; do
	case "$1" in
		--no-build) do_build=0 ;;
		--no-test) do_test=0 ;;
		--cli) shift; while [ $# -gt 0 ] && [ "${1#--}" = "$1" ]; do cli_inputs+=("$1"); shift; done; continue ;;
		*) echo "usage: $0 asan|tsan [--no-build] [--no-test] [--cli <file-or-dir>...]" >&2; exit 2 ;;
	esac
	shift
done

# llvm-symbolizer for symbolised reports (clang's runtime looks for the unversioned name only)
if [ -z "${ASAN_SYMBOLIZER_PATH:-}" ]; then
	for t in llvm-symbolizer llvm-symbolizer-17; do
		if command -v "$t" >/dev/null 2>&1; then export ASAN_SYMBOLIZER_PATH="$(command -v "$t")"; break; fi
	done
fi
if [ -n "${ASAN_SYMBOLIZER_PATH:-}" ]; then
	sym=":external_symbolizer_path=$ASAN_SYMBOLIZER_PATH"
else
	sym=""
fi
SUPP="$ROOT/scripts/sanitizers"
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:abort_on_error=1:strict_string_checks=1:detect_stack_use_after_return=1$sym}"
export LSAN_OPTIONS="${LSAN_OPTIONS:-suppressions=$SUPP/lsan.supp:print_suppressions=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1:suppressions=$SUPP/ubsan.supp$sym}"
export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1:second_deadlock_stack=1:suppressions=$SUPP/tsan.supp$sym}"

if [ "$do_build" = 1 ]; then
	cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" -DIMG2_WERROR=OFF -DIMG2_SANITIZE="$san"
	cmake --build "$BUILD_DIR" -j"$JOBS"
fi

rc=0
if [ "$do_test" = 1 ]; then
	exclude='guard|consumer|expensive|no-sanitize'
	ctest --test-dir "$BUILD_DIR" -j"$CTEST_JOBS" --output-on-failure -LE "$exclude" || rc=$?
fi

if [ "${#cli_inputs[@]}" -gt 0 ]; then
	bin="$BUILD_DIR/imgcat2"
	logs="$OUT_DIR/logs"
	out="$OUT_DIR/out"
	rm -rf "$logs" "$out"
	mkdir -p "$logs" "$out"
	export ASAN_OPTIONS="$ASAN_OPTIONS:log_path=$logs/asan"
	export UBSAN_OPTIONS="$UBSAN_OPTIONS:log_path=$logs/ubsan"
	export TSAN_OPTIONS="$TSAN_OPTIONS:log_path=$logs/tsan"
	files=()
	for p in "${cli_inputs[@]}"; do
		if [ -d "$p" ]; then
			while IFS= read -r -d '' f; do files+=("$f"); done < <(find -L "$p" -type f -print0 | sort -z)
		else
			files+=("$p")
		fi
	done
	CLI_JOBS="${CLI_JOBS:-$CTEST_JOBS}"
	CLI_TIMEOUT="${CLI_TIMEOUT:-900}"
	# run <dir> <env...> -- <args...>: one imgcat2 run; appends "run", "signal <rc> <args>" or "timeout <args>" to
	# <dir>/status (a signal exit other than the SIGINT that ends the -a loop is a failure)
	run() {
		local d="$1"
		shift
		local envs=()
		while [ "$1" != "--" ]; do envs+=("$1"); shift; done
		shift
		local r=0
		env "${envs[@]}" timeout -s INT "$CLI_TIMEOUT" "$bin" "$@" </dev/null >"$d/stdout" 2>"$d/stderr" || r=$?
		echo run >>"$d/status"
		if [ "$r" = 124 ]; then
			echo "timeout $*" >>"$d/status"
		elif [ "$r" -ge 128 ] && [ "$r" != 130 ]; then
			echo "signal $r $*" >>"$d/status"
		fi
	}
	sweep_file() {   # sweep_file <index> <file>
		local d="$out/$1"
		local f="$2"
		mkdir -p "$d"
		run "$d" TERM=xterm-256color -- -w 64 "$f"
		run "$d" TERM=xterm-kitty -- -w 64 "$f"
		run "$d" TERM=xterm-256color TERM_PROGRAM=iTerm.app -- -w 64 "$f"
		run "$d" TERM=xterm-256color TERM_PROGRAM=iTerm.app -- --iterm2-format png -w 64 "$f"
		run "$d" TERM=xterm-256color -- --info --json --exif-detailed "$f"
		run "$d" TERM=xterm-256color -- -v -r -w 40 -H 20 -i bilinear "$f"
		run "$d" TERM=xterm-256color -- -n 1 -p 1 -o "$d/n1.png" "$f"
		run "$d" TERM=xterm-256color -- -p 6 -o "$d/o.png" "$f"
		run "$d" TERM=xterm-256color -- -j 85 -o "$d/o.jpg" "$f"
		run "$d" TERM=xterm-256color -- --webp 80 -o "$d/o.webp" "$f"
		run "$d" TERM=xterm-256color -- --webp 100 -o "$d/ll.webp" "$f"
		run "$d" TERM=xterm-256color -- --heif 60 -o "$d/o.heif" "$f"
		run "$d" TERM=xterm-256color -- --jxl 80 -o "$d/o.jxl" "$f"
		run "$d" TERM=xterm-256color -- --jxl 100 -o "$d/ll.jxl" "$f"
		# animation loop, ended by SIGINT after 3 s (the CLI's signal path)
		env TERM=xterm-256color timeout -s INT 3 "$bin" -a -w 32 "$f" </dev/null >/dev/null 2>&1 || true
		echo run >>"$d/status"
		echo "$f" >"$d/input"
	}
	i=0
	for f in "${files[@]}"; do
		i=$((i + 1))
		sweep_file "$i" "$f" &
		while [ "$(jobs -rp | wc -l)" -ge "$CLI_JOBS" ]; do wait -n || true; done
	done
	wait
	shopt -s nullglob
	reports=("$logs"/*)
	runs=$(cat "$out"/*/status 2>/dev/null | awk '$1 == "run"' | wc -l)
	bad=$(cat "$out"/*/status 2>/dev/null | awk '$1 != "run"')
	nbad=$(printf '%s' "$bad" | awk 'NF' | wc -l)
	[ -z "$bad" ] || printf 'sanitize.sh: %s\n' "$bad" >&2
	echo "sanitize.sh: CLI sweep: ${#files[@]} files, $runs runs, $nbad signal exits / timeouts, ${#reports[@]} sanitizer report(s) in $logs"
	if [ "${#reports[@]}" -gt 0 ] || [ "$nbad" -gt 0 ]; then
		rc=1
	fi
fi
exit "$rc"
