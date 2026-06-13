#include "Sample_Stage1.h"

// Include the other stages that generate outputs used by this block
#include "Sample_Stage2.h"
#include "Sample_Stage3.h"

Sample_Stage1::Sample_Stage1(Output_Sample_Stage1 *o, Output_Sample_Stage2 *_s2out, Output_Sample_Stage3 *_s3out) {
  output = o;
  s2out  = _s2out;
  s3out  = _s3out;
}

void Sample_Stage1::reset_cycle() {
  tmp            = 0;
  pcv.to2_aValid = false;
  pcv.to2_a      = 0;
  pcv.to2_b      = 0;
  pcv.to3_cValid = false;
  pcv.to3_c      = 0;
}

void Sample_Stage1::cycle() {
  pcv.to2_b = (s3out->to1_b + 1) & 0x7FFFFFFF;

  pcv.to2_a      = (s2out->to1_a + s3out->to1_b + 2) & 0x7FFFFFFF;
  pcv.to2_aValid = s2out->to1_aValid;

  pcv.to3_cValid = (tmp & 1) != 0;
  pcv.to3_c      = (tmp + s2out->to1_a) & 0x7FFFFFFF;

  tmp = (tmp + 23) & 0x7FFFFFFF;
}

void Sample_Stage1::update() { *output = pcv; }

void Sample_Stage1::hash_state(Sample_hash &h) const {
  h.add(output->to2_aValid);
  h.add(output->to2_a);
  h.add(output->to2_b);
  h.add(output->to3_cValid);
  h.add(output->to3_c);
  h.add(tmp);
}
