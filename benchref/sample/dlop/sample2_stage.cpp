#include "sample2_stage.hpp"

void Sample2_stage::reset_cycle() {
  tmp.init_integer(1);
  to1_aValid.init_bool(false);
  to1_a.init_integer(0);
  to2_eValid.init_bool(false);
  to2_e.init_integer(0);
  to3_dValid.init_bool(false);
  to3_d.init_integer(0);
}
