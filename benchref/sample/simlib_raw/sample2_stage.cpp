#include "sample2_stage.hpp"

#include "livesim_types.hpp"

Sample2_stage::Sample2_stage(uint64_t _hidx) : hidx(_hidx) {}

void Sample2_stage::reset_cycle() {
  tmp        = 1;
  to1_aValid = false;
  to1_a      = 0;
  to2_eValid = false;
  to2_e      = 0;
  to3_dValid = false;
  to3_d      = 0;
}
