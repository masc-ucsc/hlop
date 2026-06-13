#pragma once

#include <cstdint>

struct Sample1_stage {
  bool     to2_aValid = false;
  uint32_t to2_a      = 0;
  uint32_t to2_b      = 0;

  bool     to3_cValid = false;
  uint32_t to3_c      = 0;

  uint32_t tmp = 0;

  void reset_cycle();
  void cycle(uint32_t s3_to1_b, bool s2_to1_aValid, uint32_t s2_to1_a);
};
