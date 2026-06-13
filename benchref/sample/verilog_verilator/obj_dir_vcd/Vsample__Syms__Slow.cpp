// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Symbol table implementation internals

#include "Vsample__pch.h"

Vsample__Syms::Vsample__Syms(VerilatedContext* contextp, const char* namep, Vsample* modelp)
    : VerilatedSyms{contextp}
    // Setup internal state of the Syms class
    , __Vm_modelp{modelp}
    // Setup top module instance
    , TOP{this, namep}
{
    // Check resources
    Verilated::stackCheck(236);
    // Setup sub module instances
    // Configure time unit / time precision
    _vm_contextp__->timeunit(-12);
    _vm_contextp__->timeprecision(-12);
    // Setup each module's pointers to their submodules
    // Setup each module's pointer back to symbol table (for public functions)
    TOP.__Vconfigure(true);
    // Setup scopes
    __Vscopep_sample__s1 = new VerilatedScope{this, "sample.s1", "s1", "<null>", 0, VerilatedScope::SCOPE_OTHER};
    __Vscopep_sample__s2 = new VerilatedScope{this, "sample.s2", "s2", "<null>", 0, VerilatedScope::SCOPE_OTHER};
    __Vscopep_sample__s3 = new VerilatedScope{this, "sample.s3", "s3", "<null>", 0, VerilatedScope::SCOPE_OTHER};
    // Setup export functions - final: 0
    // Setup export functions - final: 1
    // Setup public variables
    __Vscopep_sample__s1->varInsert("tmp", &(TOP.sample__DOT__s1__DOT__tmp), false, VLVT_UINT32, VLVD_NODIR|VLVF_PUB_RD, 0, 1 ,31,0);
    __Vscopep_sample__s1->varInsert("to2_a", &(TOP.sample__DOT__s1__DOT__to2_a), false, VLVT_UINT32, VLVD_NODIR|VLVF_PUB_RD, 0, 1 ,31,0);
    __Vscopep_sample__s1->varInsert("to2_aValid", &(TOP.sample__DOT__s1__DOT__to2_aValid), false, VLVT_UINT8, VLVD_NODIR|VLVF_PUB_RD, 0, 0);
    __Vscopep_sample__s1->varInsert("to2_b", &(TOP.sample__DOT__s1__DOT__to2_b), false, VLVT_UINT32, VLVD_NODIR|VLVF_PUB_RD, 0, 1 ,31,0);
    __Vscopep_sample__s1->varInsert("to3_c", &(TOP.sample__DOT__s1__DOT__to3_c), false, VLVT_UINT32, VLVD_NODIR|VLVF_PUB_RD, 0, 1 ,31,0);
    __Vscopep_sample__s1->varInsert("to3_cValid", &(TOP.sample__DOT__s1__DOT__to3_cValid), false, VLVT_UINT8, VLVD_NODIR|VLVF_PUB_RD, 0, 0);
    __Vscopep_sample__s2->varInsert("tmp", &(TOP.sample__DOT__s2__DOT__tmp), false, VLVT_UINT32, VLVD_NODIR|VLVF_PUB_RD, 0, 1 ,31,0);
    __Vscopep_sample__s2->varInsert("to1_a", &(TOP.sample__DOT__s2__DOT__to1_a), false, VLVT_UINT32, VLVD_NODIR|VLVF_PUB_RD, 0, 1 ,31,0);
    __Vscopep_sample__s2->varInsert("to1_aValid", &(TOP.sample__DOT__s2__DOT__to1_aValid), false, VLVT_UINT8, VLVD_NODIR|VLVF_PUB_RD, 0, 0);
    __Vscopep_sample__s2->varInsert("to2_e", &(TOP.sample__DOT__s2__DOT__to2_e), false, VLVT_UINT32, VLVD_NODIR|VLVF_PUB_RD, 0, 1 ,31,0);
    __Vscopep_sample__s2->varInsert("to2_eValid", &(TOP.sample__DOT__s2__DOT__to2_eValid), false, VLVT_UINT8, VLVD_NODIR|VLVF_PUB_RD, 0, 0);
    __Vscopep_sample__s2->varInsert("to3_d", &(TOP.sample__DOT__s2__DOT__to3_d), false, VLVT_UINT32, VLVD_NODIR|VLVF_PUB_RD, 0, 1 ,31,0);
    __Vscopep_sample__s2->varInsert("to3_dValid", &(TOP.sample__DOT__s2__DOT__to3_dValid), false, VLVT_UINT8, VLVD_NODIR|VLVF_PUB_RD, 0, 0);
    __Vscopep_sample__s3->varInsert("memory", &(TOP.sample__DOT__s3__DOT__memory), false, VLVT_UINT32, VLVD_NODIR|VLVF_PUB_RD, 1, 1 ,0,255 ,31,0);
    __Vscopep_sample__s3->varInsert("tmp", &(TOP.sample__DOT__s3__DOT__tmp), false, VLVT_UINT32, VLVD_NODIR|VLVF_PUB_RD, 0, 1 ,31,0);
    __Vscopep_sample__s3->varInsert("tmp2", &(TOP.sample__DOT__s3__DOT__tmp2), false, VLVT_UINT32, VLVD_NODIR|VLVF_PUB_RD, 0, 1 ,31,0);
    __Vscopep_sample__s3->varInsert("to1_b", &(TOP.sample__DOT__s3__DOT__to1_b), false, VLVT_UINT32, VLVD_NODIR|VLVF_PUB_RD, 0, 1 ,31,0);
}

Vsample__Syms::~Vsample__Syms() {
    // Tear down scopes
    VL_DO_CLEAR(delete __Vscopep_sample__s1, __Vscopep_sample__s1 = nullptr);
    VL_DO_CLEAR(delete __Vscopep_sample__s2, __Vscopep_sample__s2 = nullptr);
    VL_DO_CLEAR(delete __Vscopep_sample__s3, __Vscopep_sample__s3 = nullptr);
    // Tear down sub module instances
}
