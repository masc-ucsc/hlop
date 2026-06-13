// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design internal header
// See Vsample.h for the primary calling header

#ifndef VERILATED_VSAMPLE___024ROOT_H_
#define VERILATED_VSAMPLE___024ROOT_H_  // guard

#include "verilated.h"


class Vsample__Syms;

class alignas(VL_CACHE_LINE_BYTES) Vsample___024root final {
  public:

    // DESIGN SPECIFIC STATE
    VL_IN8(clk,0,0);
    VL_IN8(reset,0,0);
    CData/*0:0*/ sample__DOT__to2_aValid;
    CData/*0:0*/ sample__DOT__to3_cValid;
    CData/*0:0*/ sample__DOT__to1_aValid;
    CData/*0:0*/ sample__DOT__to2_eValid;
    CData/*0:0*/ sample__DOT__to3_dValid;
    CData/*0:0*/ sample__DOT__s1__DOT__to2_aValid;
    CData/*0:0*/ sample__DOT__s1__DOT__to3_cValid;
    CData/*0:0*/ sample__DOT__s2__DOT__to1_aValid;
    CData/*0:0*/ sample__DOT__s2__DOT__to2_eValid;
    CData/*0:0*/ sample__DOT__s2__DOT__to3_dValid;
    CData/*7:0*/ sample__DOT__s3__DOT__reset_iterator;
    CData/*0:0*/ __VstlFirstIteration;
    CData/*0:0*/ __VstlPhaseResult;
    CData/*0:0*/ __Vtrigprevexpr___TOP__clk__0;
    CData/*0:0*/ __VactPhaseResult;
    CData/*0:0*/ __VnbaPhaseResult;
    IData/*31:0*/ sample__DOT__to2_a;
    IData/*31:0*/ sample__DOT__to2_b;
    IData/*31:0*/ sample__DOT__to3_c;
    IData/*31:0*/ sample__DOT__to1_a;
    IData/*31:0*/ sample__DOT__to2_e;
    IData/*31:0*/ sample__DOT__to3_d;
    IData/*31:0*/ sample__DOT__to1_b;
    IData/*31:0*/ sample__DOT__s1__DOT__to2_a;
    IData/*31:0*/ sample__DOT__s1__DOT__to2_b;
    IData/*31:0*/ sample__DOT__s1__DOT__to3_c;
    IData/*31:0*/ sample__DOT__s1__DOT__tmp;
    IData/*31:0*/ sample__DOT__s2__DOT__to1_a;
    IData/*31:0*/ sample__DOT__s2__DOT__to2_e;
    IData/*31:0*/ sample__DOT__s2__DOT__to3_d;
    IData/*31:0*/ sample__DOT__s2__DOT__tmp;
    IData/*31:0*/ sample__DOT__s3__DOT__to1_b;
    IData/*31:0*/ sample__DOT__s3__DOT__tmp;
    IData/*31:0*/ sample__DOT__s3__DOT__tmp2;
    IData/*31:0*/ __VactIterCount;
    VlUnpacked<IData/*31:0*/, 256> sample__DOT__s3__DOT__memory;
    VlUnpacked<QData/*63:0*/, 1> __VstlTriggered;
    VlUnpacked<QData/*63:0*/, 1> __VactTriggered;
    VlUnpacked<QData/*63:0*/, 1> __VnbaTriggered;
    VlUnpacked<CData/*0:0*/, 2> __Vm_traceActivity;

    // INTERNAL VARIABLES
    Vsample__Syms* vlSymsp;
    const char* vlNamep;

    // CONSTRUCTORS
    Vsample___024root(Vsample__Syms* symsp, const char* namep);
    ~Vsample___024root();
    VL_UNCOPYABLE(Vsample___024root);

    // INTERNAL METHODS
    void __Vconfigure(bool first);
};


#endif  // guard
