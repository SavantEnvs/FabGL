#!/usr/bin/env bash
#
# mayhem/build.sh — build FabGL's fuzz harnesses, their standalone reproducers and the oracle probe.
#
# Runs inside the commit image (mayhem/Dockerfile) as `mayhem` in /mayhem. The base image
# (ghcr.io/savantenvs/base) already exports the build contract — use these, don't redefine:
#   CC, CXX             stock clang / clang++
#   LIB_FUZZING_ENGINE  -fsanitize=fuzzer   (link into each harness that has a LLVMFuzzer entry)
#   SANITIZER_FLAGS     -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
#                       (ASan + UBSan, both set to HALT — so Mayhem catches memory AND UB defects)
#   DEBUG_FLAGS         -g -gdwarf-3   (DWARF debug info — always on for fuzz/standalone builds,
#                       independent of the sanitizer off-switch; DWARF version must be < 4)
#   RUST_DEBUG_FLAGS    -C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3
#                       (thread through RUSTFLAGS on every cargo-fuzz build)
#   GO_DEBUG_FLAGS      -gcflags=all=-N -l
#                       (thread through go build / go-fuzz-build so the linked ELF keeps symbols)
#   SRC                 /mayhem (the repo source)
#
# Contract: build EVERYTHING here — one runnable binary per fuzz harness, AND the project's test
# suite (so mayhem/test.sh only has to RUN it, never compile). Keep it ADDITIVE (build upstream as
# upstream documents; don't edit upstream files). IMPORTANT: build the PROJECT ITSELF with
# $SANITIZER_FLAGS and $DEBUG_FLAGS (not just the harness) so the fuzzed code is instrumented
# AND carries DWARF < 4 symbols — otherwise ASan/UBSan only see the harness, not the library
# you're trying to find bugs in, and backtraces won't resolve project source lines. Build the TEST
# suite with the project's NORMAL flags (a clean, independent build) so test.sh stays an honest
# functional oracle and won't false-fail on benign UB. Leave the test binary/runner where test.sh
# expects it.
set -euo pipefail

# clang rejects SOURCE_DATE_EPOCH='' (empty) — it must be unset or a valid integer.
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

# Build knobs come from the ENVIRONMENT (overridable), with sane defaults — no if-statements,
# just parameter-expansion fallbacks. Default sanitizers are the base's ASan+UBSan (halting); override
# per build via the Dockerfile's `--build-arg SANITIZER_FLAGS="..."`.
# NB: SANITIZER_FLAGS uses `=` (no colon) on purpose — `=` only fills when the var is UNSET, so an
# explicit EMPTY value (`--build-arg SANITIZER_FLAGS=`) is honored and builds with NO sanitizers
# (useful when you want the program's natural crash / full backtrace, not an ASan report). The other
# knobs use `:=` (default on empty too). MAYHEM_JOBS sets build parallelism (falls back to nproc).
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
# DEBUG_FLAGS carries DWARF debug info INDEPENDENTLY of the sanitizer off-switch (so an empty
# SANITIZER_FLAGS still yields DWARF symbols). DWARF MUST be < 4 (Mayhem triage can't read >=4); clang-19's
# plain `-g` emits DWARF-5, so `-gdwarf-3` is explicit. Apply $DEBUG_FLAGS to the fuzz/harness/standalone
# builds (NOT the test/oracle build). Rust/Go carry the same intent via their language flags below.
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${RUST_DEBUG_FLAGS:=-C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3}"
: "${GO_DEBUG_FLAGS:=-gcflags=all=-N -l}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
# COVERAGE_FLAGS: empty by default → no effect on the normal oracle build. Set it via the Dockerfile's
# `--build-arg COVERAGE_FLAGS="-fprofile-instr-generate -fcoverage-mapping"` to instrument the TEST
# build for source-coverage measurement (how much of the project the test suite actually exercises —
# a quality signal for the oracle; complements the anti-reward-hack sabotage check). APPEND it to the
# test build's compile+link flags in step 3 (NOT the fuzz build); after `test.sh` runs, merge with
# `llvm-profdata` and report with `llvm-cov`. Empty value is honored (`=`, not `:=`).
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS RUST_DEBUG_FLAGS GO_DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

# FabGL is ESP32 firmware (FreeRTOS + ESP-IDF + Arduino), so the host build compiles the portable parts
# of the library (terminal, PS/2 keyboard/mouse drivers, fonts, UI widgets and the Z80/8080/6502
# emulators) against the single-threaded host shim in mayhem/shim, with -DFABGL_EMULATED.
# Relaxed UBSan check: -fno-sanitize=enum. The terminal casts the raw sequence bytes it parses to
# `enum class` types (colors, charsets, ...) by design; everything else in ASan+UBSan stays halting.
B=mayhem/.build
LIBFLAGS=(-std=gnu++17 -O1 -DFABGL_EMULATED -Imayhem/shim -Isrc -Imayhem -w)
PLAIN_FLAGS=(-std=gnu++17 -O2 -DFABGL_EMULATED -Imayhem/shim -Isrc -Imayhem -w $COVERAGE_FLAGS)
SAN_FLAGS=($SANITIZER_FLAGS $DEBUG_FLAGS -fno-sanitize=enum -fsanitize=fuzzer-no-link)

TERMINAL_LIB=(terminal fabutils displaycontroller codepages terminfo canvas fabfonts fabui
              devdrivers/keyboard devdrivers/mouse devdrivers/kbdlayouts comdrivers/ps2device)
EMU_LIB=(emudevs/Z80 emudevs/i8080 emudevs/MOS6502)

rm -rf "$B"
mkdir -p "$B/san" "$B/plain"

# 1) the library and shim, sanitized (fuzz) and plain (oracle)
compile_one() {  # <outdir> <src> <flags...>
  local out="$1" src="$2"; shift 2
  "$CXX" "$@" -c "$src" -o "$out/$(basename "${src%.*}").o"
}
export -f compile_one
export CXX
for f in "${TERMINAL_LIB[@]}" "${EMU_LIB[@]}"; do
  echo "src/$f.cpp"
done | xargs -P "$MAYHEM_JOBS" -I{} bash -c 'compile_one "$0" "$1" "${@:2}"' \
    "$B/san" "{}" "${LIBFLAGS[@]}" "${SAN_FLAGS[@]}"
compile_one "$B/san" mayhem/shim/stubs.cpp "${LIBFLAGS[@]}" "${SAN_FLAGS[@]}"
compile_one "$B/san" mayhem/lsan_off.cc $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link

for f in "${TERMINAL_LIB[@]}" "${EMU_LIB[@]}"; do
  echo "src/$f.cpp"
done | xargs -P "$MAYHEM_JOBS" -I{} bash -c 'compile_one "$0" "$1" "${@:2}"' \
    "$B/plain" "{}" "${PLAIN_FLAGS[@]}"
compile_one "$B/plain" mayhem/shim/stubs.cpp "${PLAIN_FLAGS[@]}"

# 2) harnesses: one libFuzzer binary + one -standalone reproducer each
"$CC" $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$B/standalone_main.o"

lib_objs() { local o=(); for f in "$@"; do o+=("$B/san/$(basename "$f").o"); done; echo "${o[@]}"; }
TERM_OBJS="$(lib_objs "${TERMINAL_LIB[@]}") $B/san/stubs.o"

build_harness() {  # <name> <objs...>
  local n="$1"; shift
  "$CXX" "${LIBFLAGS[@]}" "${SAN_FLAGS[@]}" -c "mayhem/$n.cpp" -o "$B/san/h_$n.o"
  "$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$B/san/h_$n.o" "$@" "$B/san/lsan_off.o" -o "/mayhem/$n"
  "$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS "$B/san/h_$n.o" "$B/standalone_main.o" "$@" "$B/san/lsan_off.o" -o "/mayhem/$n-standalone"
}
build_harness fuzz_terminal $TERM_OBJS
build_harness fuzz_keyboard $TERM_OBJS
build_harness fuzz_z80 "$B/san/Z80.o"
build_harness fuzz_i8080 "$B/san/i8080.o"
build_harness fuzz_mos6502 "$B/san/MOS6502.o"

# 3) the oracle: known-answer probe built with normal flags and dynamically linked (so that the
#    sabotage shim, which LD_PRELOADs a process-killer, takes effect on it); test.sh only runs it
PLAIN_OBJS="$(for f in "${TERMINAL_LIB[@]}" "${EMU_LIB[@]}"; do printf '%s ' "$B/plain/$(basename "$f").o"; done) $B/plain/stubs.o"
"$CXX" "${PLAIN_FLAGS[@]}" -c mayhem/kat.cpp -o "$B/plain/kat_main.o"
"$CXX" $COVERAGE_FLAGS "$B/plain/kat_main.o" $PLAIN_OBJS -o "$B/kat"
ldd "$B/kat" > /dev/null
