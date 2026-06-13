#!/usr/bin/env bash
# Build every available implementation of benchref/sample, run the
# correctness cross-check (state-hash diff against golden_hash_100k.txt),
# and print a performance comparison table. See README.md for the protocol.
#
# usage: ./run_bench.sh [all|check|bench|vcd]

set -u
cd "$(dirname "$0")"

MODE=${1:-all}

RESET=1000
CHECK_CYCLES=100000
CHECK_K=10000
BENCH_CYCLES=10000000   # slowest implementation (icarus vvp) ~20 s
VCD_CYCLES=10000

GOLDEN=golden_hash_100k.txt
BIN=../../bazel-bin/benchref/sample

have() { command -v "$1" >/dev/null 2>&1; }
now() { python3 -c 'import time; print(f"{time.time():.3f}")'; }

# ---------------------------------------------------------------- build ----
echo "== build =="

if have bazel; then
  (cd ../.. && bazel build -c opt //benchref/sample:all) || exit 1
else
  echo "bazel not found - skipping the C++ implementations"
fi

if have verilator; then
  make -C verilog_verilator vsample >/dev/null || exit 1
else
  echo "verilator not found - skipping verilog_verilator"
fi

if have iverilog; then
  make -C verilog_icarus sample.vvp >/dev/null || exit 1
else
  echo "iverilog not found - skipping verilog_icarus"
fi

if have yosys; then
  make -C verilog_yosys sample >/dev/null || exit 1
else
  echo "yosys not found - skipping verilog_yosys"
fi

if have vcs; then
  make -C verilog_vcs simv >/dev/null || exit 1
else
  echo "vcs not found - skipping verilog_vcs (commercial, optional)"
fi

# Implementations as "name|workdir|command" (flags appended per mode).
IMPLS=()
if have bazel; then
  for n in cpp_native cpp_oldprp simlib simlib_raw slop dlop; do
    IMPLS+=("$n|.|$BIN/sample_$n")
  done
fi
have verilator && IMPLS+=("verilator|verilog_verilator|./vsample")
have yosys && IMPLS+=("yosys_cxxrtl|verilog_yosys|./sample")
have iverilog && IMPLS+=("icarus|verilog_icarus|vvp sample.vvp")
have vcs && IMPLS+=("vcs|verilog_vcs|./simv")

flags() {  # flags <name> <cycles> <reset> <hash>
  case "$1" in
    icarus | vcs)
      local f="+cycles=$2 +reset=$3"
      [ "$4" != 0 ] && f="$f +hash=$4"
      echo "$f"
      ;;
    *) echo "--cycles=$2 --reset=$3 --hash=$4" ;;
  esac
}

# ---------------------------------------------------------------- check ----
fail=0
if [ "$MODE" = all ] || [ "$MODE" = check ]; then
  echo
  echo "== correctness cross-check (T=$CHECK_CYCLES R=$RESET K=$CHECK_K) =="
  for entry in "${IMPLS[@]}"; do
    IFS='|' read -r name dir cmd <<<"$entry"
    if (cd "$dir" && $cmd $(flags "$name" $CHECK_CYCLES $RESET $CHECK_K) 2>/dev/null \
          | grep -E '^(hash|memory)') | diff -q - "$GOLDEN" >/dev/null; then
      printf '  %-14s PASS\n' "$name"
    else
      printf '  %-14s FAIL (state hash differs from %s)\n' "$name" "$GOLDEN"
      fail=1
    fi
  done
fi

# ---------------------------------------------------------------- bench ----
if [ "$MODE" = all ] || [ "$MODE" = bench ]; then
  echo
  echo "== benchmark (plain run, T=$BENCH_CYCLES R=$RESET) =="
  printf '  %-14s %10s %10s\n' implementation wall_sec Mcycles/s
  for entry in "${IMPLS[@]}"; do
    IFS='|' read -r name dir cmd <<<"$entry"
    t0=$(now)
    (cd "$dir" && $cmd $(flags "$name" $BENCH_CYCLES $RESET 0) >/dev/null 2>&1)
    t1=$(now)
    printf '  %-14s %10s %10s\n' "$name" \
      "$(python3 -c "print(f'{$t1-$t0:.2f}')")" \
      "$(python3 -c "print(f'{$BENCH_CYCLES/($t1-$t0)/1e6:.1f}')")"
  done
  echo "  (wall-clock includes process/model startup; the C++ harnesses also"
  echo "   self-report a loop-only 'perf ... mcps=' line when run directly)"
fi

# ------------------------------------------------------------------ vcd ----
if [ "$MODE" = all ] || [ "$MODE" = vcd ]; then
  echo
  echo "== vcd dumps (T=$VCD_CYCLES R=$RESET) =="
  if have bazel; then
    (cd ../.. && bazel build -c opt //benchref/sample:sample_simlib_vcd) >/dev/null 2>&1 \
      && (cd simlib && "../$BIN/sample_simlib_vcd" --cycles=$VCD_CYCLES --reset=$RESET >/dev/null) \
      && echo "  simlib         simlib/dump.vcd"
  fi
  if have verilator; then
    make -C verilog_verilator vsample_vcd >/dev/null \
      && (cd verilog_verilator && ./vsample_vcd --cycles=$VCD_CYCLES --reset=$RESET >/dev/null) \
      && echo "  verilator      verilog_verilator/output.vcd"
  fi
  if have iverilog; then
    (cd verilog_icarus && vvp sample.vvp +cycles=$VCD_CYCLES +reset=$RESET +vcd >/dev/null) \
      && echo "  icarus         verilog_icarus/dump.vcd"
  fi
fi

exit $fail
