#pragma once

#include "dlop.hpp"

struct Sample1_stage {
  Dlop to2_aValid;
  Dlop to2_a;
  Dlop to2_b;

  Dlop to3_cValid;
  Dlop to3_c;

  Dlop tmp;

  void reset_cycle();
  void cycle(const Dlop& s3_to1_b, const Dlop& s2_to1_aValid, const Dlop& s2_to1_a);

private:
  // Constants are built once per stage; calling create_integer every cycle
  // would dominate cycle().
  static Dlop make_int(int64_t v) {
    Dlop d;
    d.init_integer(v);
    return d;
  }

  const Dlop k1     = make_int(1);
  const Dlop k2     = make_int(2);
  const Dlop k23    = make_int(23);
  const Dlop mask31 = make_int(0x7FFFFFFF);
};
