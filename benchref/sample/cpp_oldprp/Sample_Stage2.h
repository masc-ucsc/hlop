#ifndef STAGE2_SAMPLE_H
#define STAGE2_SAMPLE_H

#include <cstdint>

#include "Stage.h"
#include "sample_hash.hpp"

class Output_Sample_Stage1;

class Output_Sample_Stage2 {
public:
  bool     to1_aValid;
  uint32_t to1_a;

  bool     to2_eValid;
  uint32_t to2_e;

  bool     to3_dValid;
  uint32_t to3_d;
};

class Sample_Stage2 : public Stage {
protected:
  Output_Sample_Stage2 pcv;  // pending values, committed by update()

  uint32_t tmp;  // local storage register

  Output_Sample_Stage2 *output;

  Output_Sample_Stage1 *s1out;

public:
  Sample_Stage2(Output_Sample_Stage2 *o, Output_Sample_Stage1 *s1out);

  void reset_cycle() override;
  void cycle() override;
  void update() override;

  // fold the committed registers in the canonical hash order (see README.md)
  void hash_state(Sample_hash &h) const;
};

#endif
