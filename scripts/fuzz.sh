#!/usr/bin/env bash
# Coverage-guided fuzzing of libimg2 (libFuzzer + ASan/UBSan). See docs/fuzzing.md.
#
#   scripts/fuzz.sh [--no-build] [--no-run] [--replay] [target...]
#
# Targets (default: all): fuzz_decode fuzz_decode_forced fuzz_exif fuzz_xmp_xml fuzz_yuv
#
# Environment (all optional):
#   BUILD_DIR     fuzz build tree                         (default: <repo>/build-fuzz)
#   WORK_DIR      corpora, seeds, crash artifacts, logs   (default: $BUILD_DIR/fuzz-work)
#   FUZZ_TIME     seconds of fuzzing per target            (default: 600)
#   FUZZ_JOBS     parallel fuzzing processes per target    (default: 8)
#   FUZZ_MODE     fork (libFuzzer -fork, keeps going after findings) | jobs (-jobs/-workers, a job stops at
#                 its first finding)                       (default: fork)
#   BUILD_JOBS    build parallelism                        (default: 16)
#   RSS_MB        -rss_limit_mb                            (default: 2048)
#   TIMEOUT       -timeout per input, seconds              (default: 10)
#   MAX_ALLOC_MB  ASan max_allocation_size_mb: larger single allocations return NULL (allocator_may_return_null)
#                 so legitimately large images within the library limits end as IMG2_E_NOMEM instead of an
#                 OOM report                               (default: 256)
#   FUZZ_ARGS     extra libFuzzer arguments
#   CC / CXX      clang drivers                            (default: clang / clang++)
#   TMPDIR        honoured by the build and libFuzzer      (default: $WORK_DIR/tmp)
#
# Steps: configure $BUILD_DIR (IMG2_FUZZ=ON, clang, only the library and the fuzz targets), build, generate the
# seed corpora (fuzz_seedgen: synthetic inputs + src/tests/data + src/tests/fixtures), then per target fuzz for
# FUZZ_TIME seconds into $WORK_DIR/corpus/<target> with the dictionary src/tests/fuzz/dict/<dict>, writing
# findings to $WORK_DIR/artifacts/<target>/ and the log to $WORK_DIR/logs/<target>.log.
# --replay runs every corpus input once (-runs=0) and collects the recoverable UBSan reports of the vendored
# codecs into $WORK_DIR/logs/<target>.ubsan.txt (libimg2's own UB aborts during fuzzing already).
# Reproduce a finding: $BUILD_DIR/src/tests/fuzz/<target> <artifact> (IMG2_FUZZ_LOG=1 prints libimg2's log).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-fuzz}"
WORK_DIR="${WORK_DIR:-$BUILD_DIR/fuzz-work}"
FUZZ_TIME="${FUZZ_TIME:-600}"
FUZZ_JOBS="${FUZZ_JOBS:-8}"
FUZZ_MODE="${FUZZ_MODE:-fork}"
BUILD_JOBS="${BUILD_JOBS:-16}"
RSS_MB="${RSS_MB:-2048}"
TIMEOUT="${TIMEOUT:-10}"
MAX_ALLOC_MB="${MAX_ALLOC_MB:-256}"
FUZZ_ARGS="${FUZZ_ARGS:-}"
CC="${CC:-clang}"
CXX="${CXX:-clang++}"
export TMPDIR="${TMPDIR:-$WORK_DIR/tmp}"
mkdir -p "$TMPDIR" "$WORK_DIR"/{corpus,artifacts,logs}

ALL_TARGETS=(fuzz_decode fuzz_decode_forced fuzz_exif fuzz_xmp_xml fuzz_yuv)
do_build=1
do_run=1
do_replay=0
targets=()
for a in "$@"; do
	case "$a" in
		--no-build) do_build=0 ;;
		--no-run) do_run=0 ;;
		--replay) do_replay=1 ;;
		fuzz_*) targets+=("$a") ;;
		*) echo "usage: $0 [--no-build] [--no-run] [--replay] [target...]" >&2; exit 2 ;;
	esac
done
[ "${#targets[@]}" -gt 0 ] || targets=("${ALL_TARGETS[@]}")

# Per target: seed subdirectory (fuzz_seedgen), dictionary, -max_len
seed_of() { case "$1" in fuzz_decode) echo decode ;; fuzz_decode_forced) echo decode_forced ;; fuzz_exif) echo exif ;; fuzz_xmp_xml) echo xmp_xml ;; fuzz_yuv) echo yuv ;; esac; }
dict_of() { case "$1" in fuzz_decode|fuzz_decode_forced) echo decode.dict ;; fuzz_exif) echo exif.dict ;; fuzz_xmp_xml) echo xmp_xml.dict ;; fuzz_yuv) echo yuv.dict ;; esac; }
maxlen_of() { case "$1" in fuzz_decode|fuzz_decode_forced) echo 131072 ;; fuzz_exif|fuzz_xmp_xml) echo 65536 ;; fuzz_yuv) echo 16384 ;; esac; }

# The sanitizer runtime settings of every run (see MAX_ALLOC_MB). OMP_NUM_THREADS=1: jpeg-quantsmooth's OpenMP
# team per decode would oversubscribe the host with FUZZ_JOBS processes.
export ASAN_OPTIONS="allocator_may_return_null=1:max_allocation_size_mb=$MAX_ALLOC_MB:detect_leaks=1:handle_abort=1:symbolize=1${ASAN_OPTIONS:+:$ASAN_OPTIONS}"
export UBSAN_OPTIONS="print_stacktrace=1:report_error_type=1${UBSAN_OPTIONS:+:$UBSAN_OPTIONS}"
export OMP_NUM_THREADS=1

if [ "$do_build" = 1 ]; then
	# Build-time helper programs of the vendors run sanitized too: no leak checks there
	ASAN_OPTIONS=detect_leaks=0 cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" -DIMG2_FUZZ=ON -DIMG2_WERROR=OFF \
		-DIMG2_BUILD_TESTS=OFF -DIMG2_BUILD_CLI=OFF -DIMG2_INSTALL=OFF -DIMG2_FAT_ARCHIVE=OFF
	ASAN_OPTIONS=detect_leaks=0 cmake --build "$BUILD_DIR" -j"$BUILD_JOBS" --target "${ALL_TARGETS[@]}" fuzz_seedgen
fi

BIN="$BUILD_DIR/src/tests/fuzz"
SEEDS="$WORK_DIR/seeds"
if [ ! -d "$SEEDS" ]; then
	# Small repository inputs only (the large RAW/HEIF samples would only be truncated to -max_len)
	mapfile -t files < <(find "$ROOT/src/tests/data" "$ROOT/src/tests/fixtures" -type f -size -300k | sort)
	"$BIN/fuzz_seedgen" "$SEEDS" "${files[@]}"
fi

[ "$do_run" = 1 ] || [ "$do_replay" = 1 ] || exit 0

for t in "${targets[@]}"; do
	corpus="$WORK_DIR/corpus/$t"
	art="$WORK_DIR/artifacts/$t"
	mkdir -p "$corpus" "$art"
	common=(-dict="$ROOT/src/tests/fuzz/dict/$(dict_of "$t")" -max_len="$(maxlen_of "$t")" -rss_limit_mb="$RSS_MB"
		-malloc_limit_mb="$RSS_MB" -timeout="$TIMEOUT")
	if [ "$do_run" = 1 ]; then
		echo "fuzz.sh: $t for ${FUZZ_TIME}s x $FUZZ_JOBS ($FUZZ_MODE) -> $WORK_DIR/logs/$t.log"
		if [ "$FUZZ_MODE" = fork ]; then
			# shellcheck disable=SC2086
			"$BIN/$t" "${common[@]}" -fork="$FUZZ_JOBS" -ignore_crashes=1 -ignore_timeouts=1 -ignore_ooms=1 \
				-max_total_time="$FUZZ_TIME" -artifact_prefix="$art/" -print_final_stats=1 $FUZZ_ARGS \
				"$corpus" "$SEEDS/$(seed_of "$t")" > "$WORK_DIR/logs/$t.log" 2>&1 || true
		else
			# shellcheck disable=SC2086
			(cd "$art" && "$BIN/$t" "${common[@]}" -jobs="$FUZZ_JOBS" -workers="$FUZZ_JOBS" \
				-max_total_time="$FUZZ_TIME" -artifact_prefix="$art/" -print_final_stats=1 $FUZZ_ARGS \
				"$corpus" "$SEEDS/$(seed_of "$t")" > "$WORK_DIR/logs/$t.log" 2>&1) || true
		fi
		tail -n 3 "$WORK_DIR/logs/$t.log" || true
	fi
	if [ "$do_replay" = 1 ]; then
		"$BIN/$t" "${common[@]}" -runs=0 "$corpus" > "$WORK_DIR/logs/$t.replay.log" 2>&1 || true
		{ grep -E 'runtime error:' "$WORK_DIR/logs/$t.replay.log" || true; } | sed -E 's/^[^ ]*\/vendor\//vendor\//' \
			| sort | uniq -c | sort -rn > "$WORK_DIR/logs/$t.ubsan.txt"
		echo "fuzz.sh: $t replay: $(wc -l < "$WORK_DIR/logs/$t.ubsan.txt") distinct UBSan report(s) -> $WORK_DIR/logs/$t.ubsan.txt"
	fi
done
echo "fuzz.sh: findings (crash-*, leak-*, timeout-*, oom-*) in $WORK_DIR/artifacts/<target>/"
