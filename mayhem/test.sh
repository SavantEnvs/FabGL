#!/usr/bin/env bash
#
# mayhem/test.sh — RUN this repo's OWN functional test suite (already built by mayhem/build.sh).
# exit 0 = pass. EDIT per repo. PATCH-grade oracle: after an agent patches the source, the grader
# rebuilds (build.sh) then runs this. DELETE this file if the repo has no meaningful tests.
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

# FabGL ships no host-runnable test suite (it is ESP32 firmware), so the oracle is a known-answer probe,
# mayhem/kat.cpp, built by build.sh with normal flags and dynamically linked: fixed programs/inputs go
# through the real Z80, i8080 and MOS6502 emulators, the PS/2 scancode decoder and the terminal's
# escape-sequence engine, and each printed value is compared here against the architecturally known answer.
KAT="$SRC/mayhem/.build/kat"
[ -x "$KAT" ] || { echo "missing $KAT: mayhem/build.sh did not build it" >&2; emit_ctrf "fabgl-kat" 0 1 0; exit 1; }

OUT="$("$KAT" 2>&1)"
echo "$OUT"

passed=0; failed=0
expect() {  # <description> <exact expected line>
  if printf '%s\n' "$OUT" | grep -qxF -- "$2"; then
    passed=$((passed + 1))
  else
    failed=$((failed + 1)); echo "FAIL: $1: expected '$2'" >&2
  fi
}

# LD A,5; LD B,7; ADD A,B; LD (8000h),A; HALT
expect "z80 5+7 and halt"               "z80 a=12 b=7 mem=12 halted=1"
# MVI A,25h; ADI 17h; STA; MOV B,A
expect "i8080 25h+17h=3Ch"              "i8080 a=60 b=60 mem=60"
# binary 12h+30h=42h, decimal mode 19+01=20 (BCD), PC after 17 code bytes
expect "mos6502 binary and BCD add"     "mos6502 bin=66 bcd=32 pc=1041"
# set-2 scancodes 12 1C F0 1C F0 12 on the US layout: SHIFT down, 'A' down/up, SHIFT up
expect "ps2 shift down"                 "key vk=117 down=1 ascii=0 shift=0"
expect "ps2 'A' down with shift"        "key vk=48 down=1 ascii=65 shift=1"
expect "ps2 'A' up with shift"          "key vk=48 down=0 ascii=65 shift=1"
expect "ps2 shift up"                   "key vk=117 down=0 ascii=0 shift=1"
# "AB\r\nC" fills (0,0) (1,0) (0,1); cursor is 1-based
expect "terminal text and cursor"       "term text=ABC cursor=2,2"
expect "terminal CSI CUP"               "term cup glyph=Z at(10,5) cursor=11,5"
expect "terminal DA1 reply"             "term da1 reply=13 bytes first=27"

emit_ctrf "fabgl-kat" "$passed" "$failed" 0
