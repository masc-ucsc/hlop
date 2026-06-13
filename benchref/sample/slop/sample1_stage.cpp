
#include "sample1_stage.hpp"

void Sample1_stage::reset_cycle() {
  tmp        = 0;
  to2_aValid = Slop<1>::create_bool(false);
  to2_a      = 0;
  to2_b      = 0;
  to3_cValid = Slop<1>::create_bool(false);
  to3_c      = 0;
}

void Sample1_stage::cycle(Slop<32> s3_to1_b, Slop<1> s2_to1_aValid, Slop<32> s2_to1_a) {
  constexpr Slop<32> mask31(0x7FFFFFFF);

  to2_b = s3_to1_b.add_op(1).and_op(mask31);

  to2_a      = s2_to1_a.add_op(s3_to1_b).add_op(2).and_op(mask31);
  to2_aValid = s2_to1_aValid;

  to3_cValid = Slop<1>::create_bool(tmp.bit_test(0));
  to3_c      = tmp.add_op(s2_to1_a).and_op(mask31);

  tmp = tmp.add_op(23).and_op(mask31);
}
