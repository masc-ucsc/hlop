// CXXRTL driver for the benchref/sample design — canonical spec in
// ../README.md. Same CLI flags and output lines as the C++ harnesses; the
// state hash reads the flattened register members of the generated p_sample
// struct (sample.cc, produced by yosys write_cxxrtl). The memory[127] lines
// come from the $display in sample3.v, which CXXRTL emits to std::cout.

#include <stdio.h>

#include <algorithm>
#include <chrono>

#include "sample.cc"
#include "sample_hash.hpp"

using cxxrtl_design::p_sample;

// One full clock cycle: a single posedge, then the return to clk=0.
static void advance_clock(p_sample &top) {
  top.p_clk = cxxrtl::value<1>{1u};
  top.step();
  top.p_clk = cxxrtl::value<1>{0u};
  top.step();
}

// FNV-1a fold over the architectural registers, in the canonical order from
// ../README.md (same order as cpp_native's Sample_stage::state_hash). The
// flattened hierarchy mangles s1.to2_aValid into p_s1_2e_to2__aValid, etc.
static uint64_t state_hash(const p_sample &top) {
  Sample_hash h;

  h.add(top.p_s1_2e_to2__aValid.curr.data[0]);
  h.add(top.p_s1_2e_to2__a.curr.data[0]);
  h.add(top.p_s1_2e_to2__b.curr.data[0]);
  h.add(top.p_s1_2e_to3__cValid.curr.data[0]);
  h.add(top.p_s1_2e_to3__c.curr.data[0]);
  h.add(top.p_s1_2e_tmp.curr.data[0]);

  h.add(top.p_s2_2e_to1__aValid.curr.data[0]);
  h.add(top.p_s2_2e_to1__a.curr.data[0]);
  h.add(top.p_s2_2e_to2__eValid.curr.data[0]);
  h.add(top.p_s2_2e_to2__e.curr.data[0]);
  h.add(top.p_s2_2e_to3__dValid.curr.data[0]);
  h.add(top.p_s2_2e_to3__d.curr.data[0]);
  h.add(top.p_s2_2e_tmp.curr.data[0]);

  h.add(top.p_s3_2e_to1__b.curr.data[0]);
  h.add(top.p_s3_2e_tmp.curr.data[0]);
  h.add(top.p_s3_2e_tmp2.curr.data[0]);
  for (size_t i = 0; i < 256; ++i) {
    h.add(top.memory_p_s3_2e_memory[i].data[0]);
  }

  return h.h;
}

int main(int argc, char **argv) {
  Sample_args args;
  if (!args.parse(argc, argv)) {
    return 1;
  }

  p_sample top;

  top.p_clk   = cxxrtl::value<1>{0u};
  top.p_reset = cxxrtl::value<1>{1u};
  for (uint64_t i = 0; i < args.reset; ++i) {
    advance_clock(top);
  }
  top.p_reset = cxxrtl::value<1>{0u};

  auto t0 = std::chrono::steady_clock::now();

  const uint64_t chunk_size = args.hash ? args.hash : args.cycles;
  uint64_t       n          = 0;
  while (n < args.cycles) {
    const uint64_t chunk = std::min(chunk_size, args.cycles - n);
    for (uint64_t i = 0; i < chunk; ++i) {
      advance_clock(top);
    }
    n += chunk;
    if (args.hash) {
      Sample_hash::print(n, state_hash(top));
    }
  }

  auto t1 = std::chrono::steady_clock::now();

  if (args.hash == 0) {
    double sec = std::chrono::duration<double>(t1 - t0).count();
    printf("perf cycles=%llu sec=%.4f mcps=%.2f\n", static_cast<unsigned long long>(args.cycles), sec,
           static_cast<double>(args.cycles) / sec / 1e6);
  }

  return 0;
}
