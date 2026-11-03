#!/usr/bin/env bash
#
# mayhem/build.sh — builds the DALI fuzz harnesses and the known-answer self test.
#
# DALI proper needs the CUDA toolkit, a GPU and a multi-hour dependency build, so the harnesses
# compile the CPU-only, CUDA-free translation units that parse untrusted input (webdataset tar
# reader, COCO JSON lookahead parser, URI parser, libsndfile audio decoder wrapper) directly from
# the upstream tree, plus libtar and rapidjson (the revisions DALI itself pins) from /opt/toolchains.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
B="$SRC/build"
LIBTAR_SRC=/opt/toolchains/libtar-src
RAPIDJSON_INC=/opt/toolchains/rapidjson/include
mkdir -p "$B"

# libtar in three flavours: libFuzzer-instrumented (fuzz binaries), sanitized only (standalone
# reproducers) and normal (self test).
build_libtar() { # <dir> <cflags>
  local dir="$1" cflags="$2"
  rm -rf "$dir" && mkdir -p "$dir"
  cp -a "$LIBTAR_SRC/." "$dir/"
  (cd "$dir" && autoreconf -fi \
     && ./configure --disable-shared CC="$CC" CFLAGS="$cflags" \
     && make -j"$MAYHEM_JOBS" -C lib) >"$dir/build.log" 2>&1 || { cat "$dir/build.log" >&2; return 1; }
}
# Coverage instrumentation must be compiled into the fuzzed code, not only linked in.
COV_FLAGS="-fsanitize=fuzzer-no-link"
build_libtar "$B/libtar-fuzz" "$SANITIZER_FLAGS $DEBUG_FLAGS $COV_FLAGS -O1 -fPIC"
build_libtar "$B/libtar-san"  "$SANITIZER_FLAGS $DEBUG_FLAGS -O1 -fPIC"
build_libtar "$B/libtar-norm" "-O2 -fPIC $COVERAGE_FLAGS"

DALI_SRCS=(
  dali/operators/reader/loader/webdataset/tar_utils.cc
  dali/util/std_file.cc
  dali/core/error_handling.cc
  dali/core/common.cc
  dali/util/uri.cc
  dali/pipeline/util/lookahead_parser.cc
  dali/operators/decoder/audio/generic_decoder.cc
)
HARNESSES=(fuzz_tar_archive fuzz_coco_json fuzz_uri fuzz_audio_decoder)
INC=(-I"$SRC" -I"$SRC/include" -I"$RAPIDJSON_INC")
SNDFILE_LIBS="-lsndfile"

compile_all() { # <outdir> <libtar dir> <flags...>
  local out="$1" lt="$2"; shift 2
  mkdir -p "$out"
  local pids=() f
  for f in "${DALI_SRCS[@]}"; do
    $CXX -std=c++17 -c "$@" "${INC[@]}" -I"$lt/lib" -I"$lt/listhash" "$SRC/$f" \
      -o "$out/$(echo "$f" | tr / _).o" &
    pids+=($!)
  done
  for p in "${pids[@]}"; do wait "$p"; done
}

$CXX -std=c++17 $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$B/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$B/standalone_main.o"

# 1) libFuzzer binaries: project objects + harness instrumented, linked with the fuzzing engine.
FUZZ_CXXFLAGS=($SANITIZER_FLAGS $DEBUG_FLAGS $COV_FLAGS -O1 -DNDEBUG)
compile_all "$B/obj-fuzz" "$B/libtar-fuzz" "${FUZZ_CXXFLAGS[@]}"
for h in "${HARNESSES[@]}"; do
  $CXX -std=c++17 "${FUZZ_CXXFLAGS[@]}" "${INC[@]}" -I"$B/libtar-fuzz/lib" -I"$B/libtar-fuzz/listhash" \
    -c "$SRC/mayhem/harnesses/$h.cc" -o "$B/$h.fuzz.o"
  $CXX "${FUZZ_CXXFLAGS[@]}" $LIB_FUZZING_ENGINE "$B/$h.fuzz.o" "$B"/obj-fuzz/*.o "$B/lsan_off.o" \
    "$B/libtar-fuzz/lib/.libs/libtar.a" $SNDFILE_LIBS -o "$SRC/$h"
done

# 2) Standalone reproducers: sanitized only, linked with the run-once driver.
SAN_CXXFLAGS=($SANITIZER_FLAGS $DEBUG_FLAGS -O1 -DNDEBUG)
compile_all "$B/obj-san" "$B/libtar-san" "${SAN_CXXFLAGS[@]}"
for h in "${HARNESSES[@]}"; do
  $CXX -std=c++17 "${SAN_CXXFLAGS[@]}" "${INC[@]}" -I"$B/libtar-san/lib" -I"$B/libtar-san/listhash" \
    -c "$SRC/mayhem/harnesses/$h.cc" -o "$B/$h.san.o"
  $CXX "${SAN_CXXFLAGS[@]}" "$B/standalone_main.o" "$B/$h.san.o" "$B"/obj-san/*.o "$B/lsan_off.o" \
    "$B/libtar-san/lib/.libs/libtar.a" $SNDFILE_LIBS -o "$SRC/$h-standalone"
done

# 3) Self test with the project's NORMAL flags (no sanitizers) for mayhem/test.sh.
NORM_CXXFLAGS=(-O2 $COVERAGE_FLAGS)
compile_all "$B/obj-norm" "$B/libtar-norm" "${NORM_CXXFLAGS[@]}"
$CXX -std=c++17 "${NORM_CXXFLAGS[@]}" "${INC[@]}" -I"$B/libtar-norm/lib" -I"$B/libtar-norm/listhash" \
  "$SRC/mayhem/selftest.cc" "$B"/obj-norm/*.o "$B/libtar-norm/lib/.libs/libtar.a" $SNDFILE_LIBS \
  $COVERAGE_FLAGS -o "$SRC/dali_selftest"
