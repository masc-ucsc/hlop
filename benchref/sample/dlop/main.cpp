#include <stdio.h>

#include <algorithm>
#include <chrono>

#include "sample_hash.hpp"
#include "sample_stage.hpp"

int main(int argc, char **argv) {
  Sample_args args;
  if (!args.parse(argc, argv)) {
    return 1;
  }

  Sample_stage top;

  for (uint64_t i = 0; i < args.reset; ++i) {
    top.reset_cycle();
  }

  auto t0 = std::chrono::steady_clock::now();

  const uint64_t chunk_size = args.hash ? args.hash : args.cycles;
  uint64_t       n          = 0;
  while (n < args.cycles) {
    const uint64_t chunk = std::min(chunk_size, args.cycles - n);
    for (uint64_t i = 0; i < chunk; ++i) {
      top.cycle();
    }
    n += chunk;
    if (args.hash) {
      Sample_hash::print(n, top.state_hash());
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
