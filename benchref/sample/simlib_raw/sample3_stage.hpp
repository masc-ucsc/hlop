#pragma once

#include "livesim_types.hpp"

struct Sample3_stage {
  uint64_t                  hidx;
  UInt<32>                  to1_b;
  std::array<UInt<32>, 256> memory;

  uint8_t  reset_iterator = 0;
  UInt<32> tmp;
  UInt<32> tmp2;

  Sample3_stage(uint64_t _hidx);
  void reset_cycle();
  void cycle(UInt<1> s1_to3_cValid, UInt<32> s1_to3_c, UInt<1> s2_to3_dValid, UInt<32> s2_to3_d);
};
