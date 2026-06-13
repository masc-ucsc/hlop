#include "Sample_Stage2.h"

// Include the other stages that generate outputs used by this block
#include "Sample_Stage1.h"

Sample_Stage2::Sample_Stage2(Output_Sample_Stage2 *o, Output_Sample_Stage1 *_s1out) {
  output = o;
  s1out  = _s1out;
}

void Sample_Stage2::reset_cycle() {
  tmp            = 1;
  pcv.to1_aValid = false;
  pcv.to1_a      = 0;
  pcv.to2_eValid = false;
  pcv.to2_e      = 0;
  pcv.to3_dValid = false;
  pcv.to3_d      = 0;
}

void Sample_Stage2::cycle() {
  pcv.to3_dValid = (tmp & 1) == 0;
  pcv.to3_d      = (tmp + s1out->to2_b) & 0x7FFFFFFF;

  // pcv still holds last cycle's committed values, so reading to1_aValid and
  // to1_a before reassigning them below yields this stage's own old outputs
  pcv.to2_eValid = (tmp & 1) == 1 && s1out->to2_aValid && pcv.to1_aValid;
  pcv.to2_e      = (tmp + s1out->to2_a + pcv.to1_a) & 0x7FFFFFFF;

  pcv.to1_aValid = (tmp & 2) == 2;
  pcv.to1_a      = (tmp + 3) & 0x7FFFFFFF;

  tmp = (tmp + 13) & 0x7FFFFFFF;
}

void Sample_Stage2::update() { *output = pcv; }

void Sample_Stage2::hash_state(Sample_hash &h) const {
  h.add(output->to1_aValid);
  h.add(output->to1_a);
  h.add(output->to2_eValid);
  h.add(output->to2_e);
  h.add(output->to3_dValid);
  h.add(output->to3_d);
  h.add(tmp);
}
