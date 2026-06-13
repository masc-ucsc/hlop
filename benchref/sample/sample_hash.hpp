//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// FNV-1a 64-bit fold over the architectural registers, shared by the C++
// implementations of benchref/sample. The canonical register order and the
// printed line format are specified in README.md; the Verilog testbenches
// reimplement the same fold in SV and must stay bit-identical.

#pragma once

#include <cstdint>
#include <cstdio>

struct Sample_hash {
  uint64_t h = UINT64_C(0xcbf29ce484222325);

  void add(uint64_t v) {
    h ^= v;
    h *= UINT64_C(0x100000001b3);
  }

  static void print(uint64_t cycle, uint64_t h) {
    std::printf("hash %llu %016llx\n", static_cast<unsigned long long>(cycle), static_cast<unsigned long long>(h));
  }
};

// Tiny argv parser shared by the C++ mains: --cycles=T --reset=R --hash=K.
// Returns false (after printing usage) on an unknown flag.
struct Sample_args {
  uint64_t cycles = 100000;
  uint64_t reset  = 1000;
  uint64_t hash   = 0;  // 0 = hash mode off

  bool parse(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
      unsigned long long v = 0;
      if (std::sscanf(argv[i], "--cycles=%llu", &v) == 1) {
        cycles = v;
      } else if (std::sscanf(argv[i], "--reset=%llu", &v) == 1) {
        reset = v;
      } else if (std::sscanf(argv[i], "--hash=%llu", &v) == 1) {
        hash = v;
      } else {
        std::fprintf(stderr, "usage: %s [--cycles=T] [--reset=R] [--hash=K]\n", argv[0]);
        return false;
      }
    }
    if (reset < 256) {  // memory[] is cleared one entry per reset cycle (see README)
      std::fprintf(stderr, "ERROR: --reset=%llu but the design needs >= 256 reset cycles\n",
                   static_cast<unsigned long long>(reset));
      return false;
    }
    return true;
  }
};
