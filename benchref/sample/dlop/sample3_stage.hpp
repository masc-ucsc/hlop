#pragma once

#include <array>
#include <cstdint>

#include "dlop.hpp"

struct Sample3_stage {
  Dlop to1_b;

  std::array<Dlop, 256> memory;

  uint8_t reset_iterator = 0;
  Dlop    tmp;
  Dlop    tmp2;

  void reset_cycle();
  void cycle(const Dlop& s1_to3_cValid, const Dlop& s1_to3_c, const Dlop& s2_to3_dValid, const Dlop& s2_to3_d);

private:
  // Constants are built once per stage; calling create_integer every cycle
  // would dominate cycle().
  static Dlop make_int(int64_t v) {
    Dlop d;
    d.init_integer(v);
    return d;
  }

  const Dlop k1     = make_int(1);
  const Dlop k7     = make_int(7);
  const Dlop k15    = make_int(15);
  const Dlop kff    = make_int(0xff);
  const Dlop kffff  = make_int(0xFFFF);
  const Dlop k45339 = make_int(45339);
  const Dlop mask31 = make_int(0x7FFFFFFF);
};
