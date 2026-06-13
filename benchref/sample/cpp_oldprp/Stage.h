#ifndef STAGE_H
#define STAGE_H

// Legacy Pyrope compiler output style: each stage computes its next outputs
// into a pending-values struct (pcv) during cycle(), and update() commits it
// to the shared Output_* struct — a double-buffered equivalent of Verilog
// non-blocking assignment semantics.

class Stage {
public:
  virtual ~Stage() = default;

  // reset cycle (can be called many times)
  virtual void reset_cycle() = 0;

  // cycle, not called during reset
  virtual void cycle() = 0;

  // called after reset_cycle or cycle to commit the pending outputs
  virtual void update() = 0;
};

#endif
