// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Symbol table internal header
//
// Internal details; most calling programs do not need this header,
// unless using verilator public meta comments.

#ifndef VERILATED_VSAMPLE__SYMS_H_
#define VERILATED_VSAMPLE__SYMS_H_  // guard

#include "verilated.h"

// INCLUDE MODEL CLASS

#include "Vsample.h"

// INCLUDE MODULE CLASSES
#include "Vsample___024root.h"

// DPI TYPES for DPI Export callbacks (Internal use)

// SYMS CLASS (contains all model state)
class alignas(VL_CACHE_LINE_BYTES) Vsample__Syms final : public VerilatedSyms {
  public:
    // INTERNAL STATE
    Vsample* const __Vm_modelp;
    bool __Vm_activity = false;  ///< Used by trace routines to determine change occurred
    uint32_t __Vm_baseCode = 0;  ///< Used by trace routines when tracing multiple models
    VlDeleter __Vm_deleter;
    bool __Vm_didInit = false;

    // MODULE INSTANCE STATE
    Vsample___024root              TOP;

    // SCOPE NAMES
    VerilatedScope* __Vscopep_sample__s1;
    VerilatedScope* __Vscopep_sample__s2;
    VerilatedScope* __Vscopep_sample__s3;

    // CONSTRUCTORS
    Vsample__Syms(VerilatedContext* contextp, const char* namep, Vsample* modelp);
    ~Vsample__Syms();

    // METHODS
    const char* name() const { return TOP.vlNamep; }
};

#endif  // guard
