#include "sample1_stage.hpp"

void Sample1_stage::reset_cycle() {
  tmp.init_integer(0);
  to2_aValid.init_bool(false);
  to2_a.init_integer(0);
  to2_b.init_integer(0);
  to3_cValid.init_bool(false);
  to3_c.init_integer(0);
}

void Sample1_stage::cycle(const Dlop& s3_to1_b, const Dlop& s2_to1_aValid, const Dlop& s2_to1_a) {
  to2_b = s3_to1_b.add_op(k1)->and_op(mask31);

  to2_a      = s2_to1_a.add_op(s3_to1_b)->add_op(k2)->and_op(mask31);
  to2_aValid = s2_to1_aValid;

  to3_cValid.init_bool(tmp.bit_test(0));
  to3_c = tmp.add_op(s2_to1_a)->and_op(mask31);

  tmp = tmp.add_op(k23)->and_op(mask31);
}
