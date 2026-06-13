#pragma once

#include <cstdint>

struct Sample2_stage {
  bool     to1_aValid = false;
  uint32_t to1_a      = 0;

  bool     to2_eValid = false;
  uint32_t to2_e      = 0;

  bool     to3_dValid = false;
  uint32_t to3_d      = 0;

  uint32_t tmp = 0;

  void reset_cycle();
  void cycle(bool s1_to2_aValid, uint32_t s1_to2_a, uint32_t s1_to2_b) {
    to3_dValid = (tmp & 1) == 0;
    to3_d      = (tmp + s1_to2_b) & 0x7FFFFFFF;

    to2_eValid = (tmp & 1) == 1 && s1_to2_aValid && to1_aValid;
    to2_e      = (tmp + s1_to2_a + to1_a) & 0x7FFFFFFF;

    to1_aValid = (tmp & 2) == 2;
    to1_a      = (tmp + 3) & 0x7FFFFFFF;

    tmp = (tmp + 13) & 0x7FFFFFFF;
  }
};
