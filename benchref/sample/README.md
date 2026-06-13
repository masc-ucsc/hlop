# sample: 3-stage pipelined benchmark design

A small synthetic design with three pipeline stages, free-running counters, a
256-entry memory, and cross-stage feedback. Imported from `livehd/simlib`
(now retired). Several equivalent implementations exist so that new Slop/Dlop
code can be benchmarked against plain C++, the FIRRTL-derived `UInt<N>`
library, verilator, icarus, and yosys/CXXRTL.

Every implementation simulates the *same* deterministic design: the register
state (and therefore the state hash, see below) must match cycle-for-cycle
across all of them.

## Directory map

| dir                  | what                                                        | build      |
|----------------------|-------------------------------------------------------------|------------|
| `slop/`              | `Slop<N>` (hlop static) implementation                      | Bazel      |
| `dlop/`              | `Dlop` (hlop dynamic) implementation                        | Bazel      |
| `simlib/`            | `UInt<N>` stages, optional VCD dump (`-DSIMLIB_VCD`)        | Bazel + Makefile |
| `simlib_raw/`        | `UInt<N>` stages, no VCD support                            | Bazel + Makefile |
| `cpp_native/`        | plain C++ (`uint32_t`/`bool`) — the reference               | Bazel + Makefile |
| `cpp_oldprp/`        | legacy Pyrope compiler output style (double-buffered)       | Bazel + Makefile |
| `verilog_verilator/` | verilator harness (C++ driver)                              | Makefile   |
| `verilog_icarus/`    | iverilog/vvp harness (self-contained testbench)             | Makefile   |
| `verilog_yosys/`     | yosys CXXRTL flow                                           | Makefile   |
| `verilog_vcs/`       | VCS harness (same testbench as icarus; commercial, optional)| Makefile   |

`run_bench.sh` builds whatever tools are available, runs the correctness
cross-check, and prints a performance comparison table.

## Design specification

Three stages, each a struct of registers plus a `cycle()` function. All
cross-stage communication is through registers: `cycle()` reads the *previous*
cycle's values of every register it consumes (Verilog non-blocking
semantics). `M = 0x7FFFFFFF` below; `&` is bitwise AND, `x[i]` is bit `i`.

Stage 1 (inputs: `s2.to1_aValid`, `s2.to1_a`, `s3.to1_b`):

```
to2_b      <= (to1_b + 1) & M
to2_a      <= (to1_a + to1_b + 2) & M
to2_aValid <= to1_aValid
to3_cValid <= tmp[0]
to3_c      <= (tmp + to1_a) & M
tmp        <= (tmp + 23) & M
```

Stage 2 (inputs: `s1.to2_aValid`, `s1.to2_a`, `s1.to2_b`):

```
to3_dValid <= !tmp[0]
to3_d      <= (tmp + to2_b) & M
to2_eValid <= tmp[0] && to2_aValid && to1_aValid     // own to1_aValid, old value
to2_e      <= (tmp + to2_a + to1_a) & M              // own to1_a, old value
to1_aValid <= tmp[1]
to1_a      <= (tmp + 3) & M
tmp        <= (tmp + 13) & M
```

Stage 3 (inputs: `s1.to3_cValid`, `s1.to3_c`, `s2.to3_dValid`, `s2.to3_d`;
owns `memory[0..255]`):

```
if ((tmp & 0xFFFF) == 45339) {                       // old tmp, old tmp2
    if ((tmp2 & 15) == 0) print "memory[127] = <memory[127]>"   // old memory
    tmp2 <= (tmp2 + 1) & M
}
to1_b <= memory[tmp & 0xff]                          // old memory, old tmp
if (to3_cValid && to3_dValid)
    memory[(to3_c + tmp) & 0xff] <= to3_d
tmp <= (tmp + 7) & M
```

The memory read for `to1_b` always sees the *old* memory contents, even when
the same address is written in the same cycle.

### The 31-bit mask rule

The original simlib design relied on 32-bit unsigned wraparound (the counters
run forever). Slop/Dlop are signed-only with exact arithmetic, so wrap
behavior is not portable. Instead, every 32-bit register update is masked
with `M = 0x7FFFFFFF`: values stay in `[0, 2^31)`, no addition ever
overflows any implementation's arithmetic, and the bit patterns are identical
whether the implementation wraps mod 2^32 (uint32_t, UInt<32>, Verilog
`[31:0]`) or computes exactly (Slop, Dlop). This changes the numeric results
vs. the historical livehd/simlib output — intentionally; determinism across
implementations is the goal.

### Reset specification

Reset runs for `R` consecutive cycles before normal cycles start. Each reset
cycle sets (Verilog: every posedge with `reset=1`; C++: one `reset_cycle()`
call):

```
s1: tmp=0  to2_aValid=0 to2_a=0 to2_b=0 to3_cValid=0 to3_c=0
s2: tmp=1  to1_aValid=0 to1_a=0 to2_eValid=0 to2_e=0 to3_dValid=0 to3_d=0
s3: tmp=0  tmp2=0 to1_b=0
    memory[reset_iterator] = 0
    reset_iterator = (reset_iterator + 1) & 0xff     // starts at 0 at t=0
```

`reset_iterator` is 8 bits, initialized to 0 at simulation start, and walks
one memory entry per reset cycle (clear first, then increment — so the first
reset cycle clears `memory[0]`). **R must be >= 256** so the whole memory is
cleared; the protocol uses R = 1000. `reset_iterator` is reset bookkeeping,
not architectural state: it is excluded from the state hash.

The original simlib/cpp_native code left several registers uninitialized at
reset (`to2_a`, `to2_b`, `to1_a`, `to3_d`, `to2_e`, `to1_b`,
`reset_iterator`); the Verilog used `'bx`. All of that was nondeterministic
and is fixed as above in every implementation, including the Verilog.

## State hash (correctness cross-check)

The design has no top-level outputs, so implementations are compared by
hashing all architectural registers. Hash = FNV-1a 64-bit
(offset `0xcbf29ce484222325`, prime `0x100000001b3`), folding each register
as a `uint64`:

```
h = 0xcbf29ce484222325
for v in registers: h = (h ^ v) * 0x100000001b3   (mod 2^64)
```

Canonical register order (valids fold as 0 or 1):

```
s1.to2_aValid s1.to2_a s1.to2_b s1.to3_cValid s1.to3_c s1.tmp
s2.to1_aValid s2.to1_a s2.to2_eValid s2.to2_e s2.to3_dValid s2.to3_d s2.tmp
s3.to1_b s3.tmp s3.tmp2 s3.memory[0] .. s3.memory[255]
```

(272 values total. `sample_hash.hpp` implements the fold for the C++
implementations; the Verilog testbenches implement the same fold in SV.)

In hash mode an implementation prints, after post-reset cycle `n` for every
`n % K == 0` and after the final cycle:

```
hash <n> <16 lowercase hex digits>
```

Post-reset cycles are counted 1..T; the hash after cycle `n` reflects the
register state once cycle `n`'s updates are complete.

The `memory[127] = <decimal>` line from stage 3 (an original simlib artifact,
~1 print per million cycles, first at post-reset cycle 6477) is also part of
the comparable output: every implementation prints it identically
(`memory[127] = ` + unsigned decimal, no padding). Correctness comparison =
the `^hash |^memory` lines of two implementations' stdout are identical.

## Benchmark protocol

All counts are fixed and shared across implementations (driven by
`run_bench.sh`); R = 1000 reset cycles always (< 1% of the benchmark run).

| mode        | cycles T    | hash interval K | purpose                          |
|-------------|-------------|-----------------|----------------------------------|
| correctness | 100,000     | 10,000          | cross-check: hash lines must match |
| plain bench | 10,000,000  | off             | the primary performance number   |
| vcd         | 10,000      | off             | waveform dump for debug/compare  |

The plain-bench cycle count is sized so the slowest implementation (icarus
vvp, ~0.5 Mcycles/s) stays under ~20 seconds. Plain runs dump no VCD, no hashes, and no checkpoints;
wall-clock and cycles/sec are reported (C++ harnesses self-report a
`perf cycles=<T> sec=<s> mcps=<r>` line; `run_bench.sh` also measures
wall-clock externally for the Verilog flows).

The VCD run is a separate short mode (`simlib` `sample_vcd` binary, verilator
`vsample_vcd`, icarus `+vcd`) for waveform comparison — e.g. simlib's
vcd_writer output vs verilator `--trace`. It is never the performance number.

### CLI conventions

C++ binaries:

```
./sample [--cycles=T] [--reset=R] [--hash=K]
```

Defaults: T=100000, R=1000, hash off. Verilog testbenches (icarus/vcs) take
plusargs: `+cycles=T +reset=R +hash=K +vcd`. The verilator and CXXRTL
harnesses take the same `--` flags as the C++ binaries.

## Running

```
./run_bench.sh            # build + correctness cross-check + benchmark table
```

Bazel builds all six C++ implementations: `bazel build -c opt
//benchref/sample:all`. Always benchmark `-c opt` binaries — the fastbuild
default keeps iassert checks, which slows slop ~70x and dlop ~6x. The moved
dirs (`simlib`, `simlib_raw`, `cpp_native`, `cpp_oldprp`) also keep standalone
Makefiles (`make && ./sample`); `slop`/`dlop` are Bazel-only since they link
hlop and its external deps. The Verilog flows are Makefile-only (they shell
out to verilator/iverilog/yosys; `verilog_vcs` needs a commercial VCS install
and is skipped by `run_bench.sh` when absent).

Indicative rates on an Apple-Silicon dev box at import time (plain mode,
Mcycles/s): simlib/simlib_raw ~286, cpp_native ~253, cpp_oldprp ~247,
slop ~225, yosys/CXXRTL ~65, verilator ~55, dlop ~3.3, icarus ~0.5.

`golden_hash_100k.txt` holds the expected `^hash |^memory` output of the
correctness run (T=100k, R=1000, K=10k), generated from `cpp_native` and
verified against every other implementation.

## Provenance and licenses

Imported from `livehd/simlib` (design by the LiveHD authors). `uint.hpp` /
`sint.hpp` in `benchref/` derive from the FIRRTL signed/unsigned int library
(`LICENSE.firrtl-sig`); `vcd_writer.{hpp,cpp}` / `vcd_utils.hpp` from
vcd-writer (`LICENSE.vcd-writer`). simlib's half-finished checkpoint/signature
code was intentionally *not* imported (see `todo_checkpoint.md` at the repo
root for the distilled ideas); `SIMLIB_TRACE`/`add_signature` blocks were
stripped from the moved sources.
