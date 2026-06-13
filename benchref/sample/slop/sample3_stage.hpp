#pragma once

#include <array>
#include <cstdint>

#include "slop.hpp"

struct Sample3_stage {
  Slop<32> to1_b = 0;

  std::array<Slop<32>, 256> memory{};

  uint8_t  reset_iterator = 0;
  Slop<32> tmp            = 0;
  Slop<32> tmp2           = 0;

  void reset_cycle();
  void cycle(Slop<1> s1_to3_cValid, Slop<32> s1_to3_c, Slop<1> s2_to3_dValid, Slop<32> s2_to3_d);
};
