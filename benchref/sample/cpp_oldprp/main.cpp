
#include <stdio.h>

#include <algorithm>
#include <chrono>

#include "Sample_Stage1.h"
#include "Sample_Stage2.h"
#include "Sample_Stage3.h"
#include "sample_hash.hpp"

static uint64_t state_hash(const Sample_Stage1 &s1, const Sample_Stage2 &s2, const Sample_Stage3 &s3) {
  Sample_hash h;
  s1.hash_state(h);
  s2.hash_state(h);
  s3.hash_state(h);
  return h.h;
}

int main(int argc, char **argv) {
  Sample_args args;
  if (!args.parse(argc, argv)) {
    return 1;
  }

  // Shared outputs, double-buffered by each stage's update()
  Output_Sample_Stage1 o1;
  Output_Sample_Stage2 o2;
  Output_Sample_Stage3 o3;

  Sample_Stage1 s1(&o1, &o2, &o3);
  Sample_Stage2 s2(&o2, &o1);
  Sample_Stage3 s3(&o3, &o1, &o2);

  for (uint64_t i = 0; i < args.reset; ++i) {
    s1.reset_cycle();
    s2.reset_cycle();
    s3.reset_cycle();
    s1.update();
    s2.update();
    s3.update();
  }

  auto t0 = std::chrono::steady_clock::now();

  const uint64_t chunk_size = args.hash ? args.hash : args.cycles;
  uint64_t       n          = 0;
  while (n < args.cycles) {
    const uint64_t chunk = std::min(chunk_size, args.cycles - n);
    for (uint64_t i = 0; i < chunk; ++i) {
      s1.cycle();
      s2.cycle();
      s3.cycle();
      s1.update();
      s2.update();
      s3.update();
    }
    n += chunk;
    if (args.hash) {
      Sample_hash::print(n, state_hash(s1, s2, s3));
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
