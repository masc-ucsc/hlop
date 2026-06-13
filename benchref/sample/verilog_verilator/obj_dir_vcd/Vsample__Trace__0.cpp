// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Tracing implementation internals

#include "verilated_vcd_c.h"
#include "Vsample__Syms.h"


void Vsample___024root__trace_chg_0_sub_0(Vsample___024root* vlSelf, VerilatedVcd::Buffer* bufp);

void Vsample___024root__trace_chg_0(void* voidSelf, VerilatedVcd::Buffer* bufp) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root__trace_chg_0\n"); );
    // Body
    Vsample___024root* const __restrict vlSelf VL_ATTR_UNUSED = static_cast<Vsample___024root*>(voidSelf);
    Vsample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    if (VL_UNLIKELY(!vlSymsp->__Vm_activity)) return;
    Vsample___024root__trace_chg_0_sub_0((&vlSymsp->TOP), bufp);
}

void Vsample___024root__trace_chg_0_sub_0(Vsample___024root* vlSelf, VerilatedVcd::Buffer* bufp) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root__trace_chg_0_sub_0\n"); );
    Vsample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    uint32_t* const oldp VL_ATTR_UNUSED = bufp->oldp(vlSymsp->__Vm_baseCode + 0);
    if (VL_UNLIKELY((vlSelfRef.__Vm_traceActivity[1U]))) {
        bufp->chgBit(oldp+0,(vlSelfRef.sample__DOT__to2_aValid));
        bufp->chgIData(oldp+1,(vlSelfRef.sample__DOT__to2_a),32);
        bufp->chgIData(oldp+2,(vlSelfRef.sample__DOT__to2_b),32);
        bufp->chgBit(oldp+3,(vlSelfRef.sample__DOT__to3_cValid));
        bufp->chgIData(oldp+4,(vlSelfRef.sample__DOT__to3_c),32);
        bufp->chgBit(oldp+5,(vlSelfRef.sample__DOT__to1_aValid));
        bufp->chgIData(oldp+6,(vlSelfRef.sample__DOT__to1_a),32);
        bufp->chgBit(oldp+7,(vlSelfRef.sample__DOT__to2_eValid));
        bufp->chgIData(oldp+8,(vlSelfRef.sample__DOT__to2_e),32);
        bufp->chgBit(oldp+9,(vlSelfRef.sample__DOT__to3_dValid));
        bufp->chgIData(oldp+10,(vlSelfRef.sample__DOT__to3_d),32);
        bufp->chgIData(oldp+11,(vlSelfRef.sample__DOT__to1_b),32);
        bufp->chgCData(oldp+12,(vlSelfRef.sample__DOT__s3__DOT__reset_iterator),8);
    }
    bufp->chgBit(oldp+13,(vlSelfRef.clk));
    bufp->chgBit(oldp+14,(vlSelfRef.reset));
    bufp->chgIData(oldp+15,(vlSelfRef.sample__DOT__s1__DOT__tmp),32);
    bufp->chgIData(oldp+16,(vlSelfRef.sample__DOT__s2__DOT__tmp),32);
    bufp->chgIData(oldp+17,(vlSelfRef.sample__DOT__s3__DOT__tmp),32);
    bufp->chgIData(oldp+18,(vlSelfRef.sample__DOT__s3__DOT__tmp2),32);
}

void Vsample___024root__trace_cleanup(void* voidSelf, VerilatedVcd* /*unused*/) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root__trace_cleanup\n"); );
    // Body
    Vsample___024root* const __restrict vlSelf VL_ATTR_UNUSED = static_cast<Vsample___024root*>(voidSelf);
    Vsample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    vlSymsp->__Vm_activity = false;
    vlSymsp->TOP.__Vm_traceActivity[0U] = 0U;
    vlSymsp->TOP.__Vm_traceActivity[1U] = 0U;
}
