#pragma once

#include "dlop.hpp"

struct Sample2_stage {
  Dlop to1_aValid;
  Dlop to1_a;

  Dlop to2_eValid;
  Dlop to2_e;

  Dlop to3_dValid;
  Dlop to3_d;

  Dlop tmp;

  void reset_cycle();
  void cycle(const Dlop& s1_to2_aValid, const Dlop& s1_to2_a, const Dlop& s1_to2_b) {
    to3_dValid.init_bool(!tmp.bit_test(0));
    to3_d = tmp.add_op(s1_to2_b)->and_op(mask31);

    to2_eValid.init_bool(tmp.bit_test(0) && s1_to2_aValid.is_known_true() && to1_aValid.is_known_true());
    to2_e = tmp.add_op(s1_to2_a)->add_op(to1_a)->and_op(mask31);

    to1_aValid.init_bool(tmp.bit_test(1));
    to1_a = tmp.add_op(k3)->and_op(mask31);

    tmp = tmp.add_op(k13)->and_op(mask31);
  }

private:
  // Constants are built once per stage; calling create_integer every cycle
  // would dominate cycle().
  static Dlop make_int(int64_t v) {
    Dlop d;
    d.init_integer(v);
    return d;
  }

  const Dlop k3     = make_int(3);
  const Dlop k13    = make_int(13);
  const Dlop mask31 = make_int(0x7FFFFFFF);
};
