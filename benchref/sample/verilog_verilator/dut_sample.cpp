// Verilator driver for the benchref/sample design — canonical spec in
// ../README.md. Same CLI flags and output lines as the C++ harnesses; the
// state hash reads the /*verilator public_flat_rd*/ registers through the
// generated model. Build with -DTRACE (vsample_vcd) to dump output.vcd.

#include <stdio.h>

#include <algorithm>
#include <chrono>

#include "Vsample.h"
#include "Vsample___024root.h"
#include "sample_hash.hpp"
#include "verilated.h"
#ifdef TRACE
#include "verilated_vcd_c.h"
#endif

static vluint64_t global_time = 0;
#ifdef TRACE
static VerilatedVcdC *tfp = nullptr;
#endif

// One full clock cycle: a single posedge, then the return to clk=0.
static void advance_clock(Vsample *uut) {
  uut->clk = 1;
  uut->eval();
#ifdef TRACE
  tfp->dump(global_time);
#endif
  global_time++;

  uut->clk = 0;
  uut->eval();
#ifdef TRACE
  tfp->dump(global_time);
#endif
  global_time++;
}

// FNV-1a fold over the architectural registers, in the canonical order from
// ../README.md (same order as cpp_native's Sample_stage::state_hash).
static uint64_t state_hash(const Vsample *uut) {
  const auto *rp = uut->rootp;

  Sample_hash h;

  h.add(rp->sample__DOT__s1__DOT__to2_aValid);
  h.add(rp->sample__DOT__s1__DOT__to2_a);
  h.add(rp->sample__DOT__s1__DOT__to2_b);
  h.add(rp->sample__DOT__s1__DOT__to3_cValid);
  h.add(rp->sample__DOT__s1__DOT__to3_c);
  h.add(rp->sample__DOT__s1__DOT__tmp);

  h.add(rp->sample__DOT__s2__DOT__to1_aValid);
  h.add(rp->sample__DOT__s2__DOT__to1_a);
  h.add(rp->sample__DOT__s2__DOT__to2_eValid);
  h.add(rp->sample__DOT__s2__DOT__to2_e);
  h.add(rp->sample__DOT__s2__DOT__to3_dValid);
  h.add(rp->sample__DOT__s2__DOT__to3_d);
  h.add(rp->sample__DOT__s2__DOT__tmp);

  h.add(rp->sample__DOT__s3__DOT__to1_b);
  h.add(rp->sample__DOT__s3__DOT__tmp);
  h.add(rp->sample__DOT__s3__DOT__tmp2);
  for (int i = 0; i < 256; ++i) {
    h.add(rp->sample__DOT__s3__DOT__memory[i]);
  }

  return h.h;
}

int main(int argc, char **argv) {
  Sample_args args;
#ifdef TRACE
  args.cycles = 10000;  // VCD runs default short (see README); --cycles still rules
#endif
  if (!args.parse(argc, argv)) {
    return 1;
  }

  Vsample uut;
#ifdef TRACE
  Verilated::traceEverOn(true);
  tfp = new VerilatedVcdC;
  uut.trace(tfp, 99);
  tfp->open("output.vcd");
#endif

  uut.clk   = 0;
  uut.reset = 1;
  for (uint64_t i = 0; i < args.reset; ++i) {
    advance_clock(&uut);
  }
  uut.reset = 0;

  auto t0 = std::chrono::steady_clock::now();

  const uint64_t chunk_size = args.hash ? args.hash : args.cycles;
  uint64_t       n          = 0;
  while (n < args.cycles) {
    const uint64_t chunk = std::min(chunk_size, args.cycles - n);
    for (uint64_t i = 0; i < chunk; ++i) {
      advance_clock(&uut);
    }
    n += chunk;
    if (args.hash) {
      Sample_hash::print(n, state_hash(&uut));
    }
  }

  auto t1 = std::chrono::steady_clock::now();

#ifdef TRACE
  tfp->close();
#endif
  uut.final();

  if (args.hash == 0) {
    double sec = std::chrono::duration<double>(t1 - t0).count();
    printf("perf cycles=%llu sec=%.4f mcps=%.2f\n", static_cast<unsigned long long>(args.cycles), sec,
           static_cast<double>(args.cycles) / sec / 1e6);
  }

  return 0;
}
