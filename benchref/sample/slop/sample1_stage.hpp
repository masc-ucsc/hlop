#pragma once

#include "slop.hpp"

struct Sample1_stage {
  Slop<1>  to2_aValid = Slop<1>::create_bool(false);
  Slop<32> to2_a      = 0;
  Slop<32> to2_b      = 0;

  Slop<1>  to3_cValid = Slop<1>::create_bool(false);
  Slop<32> to3_c      = 0;

  Slop<32> tmp = 0;

  void reset_cycle();
  void cycle(Slop<32> s3_to1_b, Slop<1> s2_to1_aValid, Slop<32> s2_to1_a);
};
