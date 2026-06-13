#ifndef STAGE3_SAMPLE_H
#define STAGE3_SAMPLE_H

#include <cstdint>
#include <vector>

#include "Stage.h"
#include "sample_hash.hpp"

class Output_Sample_Stage1;
class Output_Sample_Stage2;

class Output_Sample_Stage3 {
public:
  uint32_t to1_b;
};

class Sample_Stage3 : public Stage {
protected:
  Output_Sample_Stage3 pcv;  // pending values, committed by update()

  std::vector<uint32_t> memory;

  uint8_t  reset_iterator;  // reset bookkeeping, excluded from the state hash
  uint32_t tmp;             // local storage register
  uint32_t tmp2;            // local storage register

  Output_Sample_Stage3 *output;

  Output_Sample_Stage1 *s1out;
  Output_Sample_Stage2 *s2out;

public:
  Sample_Stage3(Output_Sample_Stage3 *o, Output_Sample_Stage1 *s1out, Output_Sample_Stage2 *s2out);

  void reset_cycle() override;
  void cycle() override;
  void update() override;

  // fold the committed registers in the canonical hash order (see README.md)
  void hash_state(Sample_hash &h) const;
};

#endif
