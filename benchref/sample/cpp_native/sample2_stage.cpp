
#include "sample2_stage.hpp"

void Sample2_stage::reset_cycle() {
  tmp        = 1;
  to1_aValid = false;
  to1_a      = 0;
  to2_eValid = false;
  to2_e      = 0;
  to3_dValid = false;
  to3_d      = 0;
}
