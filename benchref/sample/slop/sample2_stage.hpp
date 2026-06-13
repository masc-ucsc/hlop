#pragma once

#include "slop.hpp"

struct Sample2_stage {
  Slop<1>  to1_aValid = Slop<1>::create_bool(false);
  Slop<32> to1_a      = 0;

  Slop<1>  to2_eValid = Slop<1>::create_bool(false);
  Slop<32> to2_e      = 0;

  Slop<1>  to3_dValid = Slop<1>::create_bool(false);
  Slop<32> to3_d      = 0;

  Slop<32> tmp = 0;

  void reset_cycle();
  void cycle(Slop<1> s1_to2_aValid, Slop<32> s1_to2_a, Slop<32> s1_to2_b) {
    constexpr Slop<32> mask31(0x7FFFFFFF);

    to3_dValid = Slop<1>::create_bool(!tmp.bit_test(0));
    to3_d      = tmp.add_op(s1_to2_b).and_op(mask31);

    to2_eValid = Slop<1>::create_bool(tmp.bit_test(0) && s1_to2_aValid.is_known_true() && to1_aValid.is_known_true());
    to2_e      = tmp.add_op(s1_to2_a).add_op(to1_a).and_op(mask31);

    to1_aValid = Slop<1>::create_bool(tmp.bit_test(1));
    to1_a      = tmp.add_op(3).and_op(mask31);

    tmp = tmp.add_op(13).and_op(mask31);
  }
};
