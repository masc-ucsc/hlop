// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design implementation internals
// See Vsample.h for the primary calling header

#include "Vsample__pch.h"

void Vsample___024root___eval_triggers_vec__act(Vsample___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root___eval_triggers_vec__act\n"); );
    Vsample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    vlSelfRef.__VactTriggered[0U] = (QData)((IData)(
                                                    ((IData)(vlSelfRef.clk) 
                                                     & (~ (IData)(vlSelfRef.__Vtrigprevexpr___TOP__clk__0)))));
    vlSelfRef.__Vtrigprevexpr___TOP__clk__0 = vlSelfRef.clk;
}

bool Vsample___024root___trigger_anySet__act(const VlUnpacked<QData/*63:0*/, 1> &in) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root___trigger_anySet__act\n"); );
    // Locals
    IData/*31:0*/ n;
    // Body
    n = 0U;
    do {
        if (in[n]) {
            return (1U);
        }
        n = ((IData)(1U) + n);
    } while ((1U > n));
    return (0U);
}

void Vsample___024root___nba_sequent__TOP__0(Vsample___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root___nba_sequent__TOP__0\n"); );
    Vsample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Locals
    IData/*31:0*/ __Vdly__sample__DOT__s1__DOT__tmp;
    __Vdly__sample__DOT__s1__DOT__tmp = 0;
    IData/*31:0*/ __Vdly__sample__DOT__s2__DOT__tmp;
    __Vdly__sample__DOT__s2__DOT__tmp = 0;
    IData/*31:0*/ __Vdly__sample__DOT__s3__DOT__tmp;
    __Vdly__sample__DOT__s3__DOT__tmp = 0;
    IData/*31:0*/ __Vdly__sample__DOT__s3__DOT__tmp2;
    __Vdly__sample__DOT__s3__DOT__tmp2 = 0;
    CData/*7:0*/ __VdlyDim0__sample__DOT__s3__DOT__memory__v0;
    __VdlyDim0__sample__DOT__s3__DOT__memory__v0 = 0;
    CData/*0:0*/ __VdlySet__sample__DOT__s3__DOT__memory__v0;
    __VdlySet__sample__DOT__s3__DOT__memory__v0 = 0;
    IData/*31:0*/ __VdlyVal__sample__DOT__s3__DOT__memory__v1;
    __VdlyVal__sample__DOT__s3__DOT__memory__v1 = 0;
    CData/*7:0*/ __VdlyDim0__sample__DOT__s3__DOT__memory__v1;
    __VdlyDim0__sample__DOT__s3__DOT__memory__v1 = 0;
    CData/*0:0*/ __VdlySet__sample__DOT__s3__DOT__memory__v1;
    __VdlySet__sample__DOT__s3__DOT__memory__v1 = 0;
    // Body
    __Vdly__sample__DOT__s1__DOT__tmp = vlSelfRef.sample__DOT__s1__DOT__tmp;
    __Vdly__sample__DOT__s2__DOT__tmp = vlSelfRef.sample__DOT__s2__DOT__tmp;
    __Vdly__sample__DOT__s3__DOT__tmp = vlSelfRef.sample__DOT__s3__DOT__tmp;
    __Vdly__sample__DOT__s3__DOT__tmp2 = vlSelfRef.sample__DOT__s3__DOT__tmp2;
    __VdlySet__sample__DOT__s3__DOT__memory__v0 = 0U;
    __VdlySet__sample__DOT__s3__DOT__memory__v1 = 0U;
    if (vlSelfRef.reset) {
        __Vdly__sample__DOT__s1__DOT__tmp = 0U;
        __Vdly__sample__DOT__s2__DOT__tmp = 1U;
        __Vdly__sample__DOT__s3__DOT__tmp = 0U;
        __Vdly__sample__DOT__s3__DOT__tmp2 = 0U;
        __VdlyDim0__sample__DOT__s3__DOT__memory__v0 
            = vlSelfRef.sample__DOT__s3__DOT__reset_iterator;
        __VdlySet__sample__DOT__s3__DOT__memory__v0 = 1U;
        vlSelfRef.sample__DOT__s3__DOT__reset_iterator 
            = (0x000000ffU & ((IData)(1U) + (IData)(vlSelfRef.sample__DOT__s3__DOT__reset_iterator)));
        vlSelfRef.sample__DOT__to2_e = 0U;
        vlSelfRef.sample__DOT__to3_c = 0U;
        vlSelfRef.sample__DOT__to3_d = 0U;
        vlSelfRef.sample__DOT__to2_a = 0U;
        vlSelfRef.sample__DOT__to2_b = 0U;
        vlSelfRef.sample__DOT__to1_a = 0U;
        vlSelfRef.sample__DOT__to1_b = 0U;
    } else {
        __Vdly__sample__DOT__s1__DOT__tmp = (0x7fffffffU 
                                             & ((IData)(0x00000017U) 
                                                + vlSelfRef.sample__DOT__s1__DOT__tmp));
        __Vdly__sample__DOT__s2__DOT__tmp = (0x7fffffffU 
                                             & ((IData)(0x0000000dU) 
                                                + vlSelfRef.sample__DOT__s2__DOT__tmp));
        __Vdly__sample__DOT__s3__DOT__tmp = (0x7fffffffU 
                                             & ((IData)(7U) 
                                                + vlSelfRef.sample__DOT__s3__DOT__tmp));
        if ((0x0000b11bU == (0x0000ffffU & vlSelfRef.sample__DOT__s3__DOT__tmp))) {
            if (VL_UNLIKELY(((0U == (0x0000000fU & vlSelfRef.sample__DOT__s3__DOT__tmp2))))) {
                VL_WRITEF_NX("memory[127] = %0d\n",1
                             , '#',32,vlSelfRef.sample__DOT__s3__DOT__memory[127U]);
            }
            __Vdly__sample__DOT__s3__DOT__tmp2 = (0x7fffffffU 
                                                  & ((IData)(1U) 
                                                     + vlSelfRef.sample__DOT__s3__DOT__tmp2));
        }
        if (((IData)(vlSelfRef.sample__DOT__to3_cValid) 
             & (IData)(vlSelfRef.sample__DOT__to3_dValid))) {
            __VdlyVal__sample__DOT__s3__DOT__memory__v1 
                = vlSelfRef.sample__DOT__to3_d;
            __VdlyDim0__sample__DOT__s3__DOT__memory__v1 
                = (0x000000ffU & (vlSelfRef.sample__DOT__to3_c 
                                  + vlSelfRef.sample__DOT__s3__DOT__tmp));
            __VdlySet__sample__DOT__s3__DOT__memory__v1 = 1U;
        }
        vlSelfRef.sample__DOT__to2_e = (0x7fffffffU 
                                        & ((vlSelfRef.sample__DOT__s2__DOT__tmp 
                                            + vlSelfRef.sample__DOT__to2_a) 
                                           + vlSelfRef.sample__DOT__to1_a));
        vlSelfRef.sample__DOT__to3_c = (0x7fffffffU 
                                        & (vlSelfRef.sample__DOT__s1__DOT__tmp 
                                           + vlSelfRef.sample__DOT__to1_a));
        vlSelfRef.sample__DOT__to3_d = (0x7fffffffU 
                                        & (vlSelfRef.sample__DOT__s2__DOT__tmp 
                                           + vlSelfRef.sample__DOT__to2_b));
        vlSelfRef.sample__DOT__to2_a = (0x7fffffffU 
                                        & ((IData)(2U) 
                                           + (vlSelfRef.sample__DOT__to1_a 
                                              + vlSelfRef.sample__DOT__to1_b)));
        vlSelfRef.sample__DOT__to2_b = (0x7fffffffU 
                                        & ((IData)(1U) 
                                           + vlSelfRef.sample__DOT__to1_b));
        vlSelfRef.sample__DOT__to1_a = (0x7fffffffU 
                                        & ((IData)(3U) 
                                           + vlSelfRef.sample__DOT__s2__DOT__tmp));
        vlSelfRef.sample__DOT__to1_b = vlSelfRef.sample__DOT__s3__DOT__memory
            [(0x000000ffU & vlSelfRef.sample__DOT__s3__DOT__tmp)];
    }
    vlSelfRef.sample__DOT__to2_eValid = ((1U & (~ (IData)(vlSelfRef.reset))) 
                                         && ((vlSelfRef.sample__DOT__s2__DOT__tmp 
                                              & (IData)(vlSelfRef.sample__DOT__to2_aValid)) 
                                             & (IData)(vlSelfRef.sample__DOT__to1_aValid)));
    vlSelfRef.sample__DOT__s3__DOT__tmp2 = __Vdly__sample__DOT__s3__DOT__tmp2;
    vlSelfRef.sample__DOT__to3_cValid = ((1U & (~ (IData)(vlSelfRef.reset))) 
                                         && (1U & vlSelfRef.sample__DOT__s1__DOT__tmp));
    vlSelfRef.sample__DOT__to3_dValid = ((1U & (~ (IData)(vlSelfRef.reset))) 
                                         && (1U & (~ vlSelfRef.sample__DOT__s2__DOT__tmp)));
    vlSelfRef.sample__DOT__s2__DOT__to2_eValid = vlSelfRef.sample__DOT__to2_eValid;
    vlSelfRef.sample__DOT__to2_aValid = ((1U & (~ (IData)(vlSelfRef.reset))) 
                                         && (IData)(vlSelfRef.sample__DOT__to1_aValid));
    vlSelfRef.sample__DOT__s2__DOT__to2_e = vlSelfRef.sample__DOT__to2_e;
    vlSelfRef.sample__DOT__s1__DOT__tmp = __Vdly__sample__DOT__s1__DOT__tmp;
    vlSelfRef.sample__DOT__s1__DOT__to3_cValid = vlSelfRef.sample__DOT__to3_cValid;
    vlSelfRef.sample__DOT__s2__DOT__to3_dValid = vlSelfRef.sample__DOT__to3_dValid;
    vlSelfRef.sample__DOT__s1__DOT__to3_c = vlSelfRef.sample__DOT__to3_c;
    vlSelfRef.sample__DOT__s2__DOT__to3_d = vlSelfRef.sample__DOT__to3_d;
    vlSelfRef.sample__DOT__s1__DOT__to2_aValid = vlSelfRef.sample__DOT__to2_aValid;
    vlSelfRef.sample__DOT__to1_aValid = ((1U & (~ (IData)(vlSelfRef.reset))) 
                                         && (1U & (vlSelfRef.sample__DOT__s2__DOT__tmp 
                                                   >> 1U)));
    vlSelfRef.sample__DOT__s1__DOT__to2_a = vlSelfRef.sample__DOT__to2_a;
    vlSelfRef.sample__DOT__s2__DOT__tmp = __Vdly__sample__DOT__s2__DOT__tmp;
    vlSelfRef.sample__DOT__s1__DOT__to2_b = vlSelfRef.sample__DOT__to2_b;
    vlSelfRef.sample__DOT__s2__DOT__to1_aValid = vlSelfRef.sample__DOT__to1_aValid;
    vlSelfRef.sample__DOT__s2__DOT__to1_a = vlSelfRef.sample__DOT__to1_a;
    vlSelfRef.sample__DOT__s3__DOT__tmp = __Vdly__sample__DOT__s3__DOT__tmp;
    if (__VdlySet__sample__DOT__s3__DOT__memory__v0) {
        vlSelfRef.sample__DOT__s3__DOT__memory[__VdlyDim0__sample__DOT__s3__DOT__memory__v0] = 0U;
    }
    if (__VdlySet__sample__DOT__s3__DOT__memory__v1) {
        vlSelfRef.sample__DOT__s3__DOT__memory[__VdlyDim0__sample__DOT__s3__DOT__memory__v1] 
            = __VdlyVal__sample__DOT__s3__DOT__memory__v1;
    }
    vlSelfRef.sample__DOT__s3__DOT__to1_b = vlSelfRef.sample__DOT__to1_b;
}

void Vsample___024root___eval_nba(Vsample___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root___eval_nba\n"); );
    Vsample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    if ((1ULL & vlSelfRef.__VnbaTriggered[0U])) {
        Vsample___024root___nba_sequent__TOP__0(vlSelf);
        vlSelfRef.__Vm_traceActivity[1U] = 1U;
    }
}

void Vsample___024root___trigger_orInto__act_vec_vec(VlUnpacked<QData/*63:0*/, 1> &out, const VlUnpacked<QData/*63:0*/, 1> &in) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root___trigger_orInto__act_vec_vec\n"); );
    // Locals
    IData/*31:0*/ n;
    // Body
    n = 0U;
    do {
        out[n] = (out[n] | in[n]);
        n = ((IData)(1U) + n);
    } while ((0U >= n));
}

#ifdef VL_DEBUG
VL_ATTR_COLD void Vsample___024root___dump_triggers__act(const VlUnpacked<QData/*63:0*/, 1> &triggers, const std::string &tag);
#endif  // VL_DEBUG

bool Vsample___024root___eval_phase__act(Vsample___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root___eval_phase__act\n"); );
    Vsample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    Vsample___024root___eval_triggers_vec__act(vlSelf);
#ifdef VL_DEBUG
    if (VL_UNLIKELY(vlSymsp->_vm_contextp__->debug())) {
        Vsample___024root___dump_triggers__act(vlSelfRef.__VactTriggered, "act"s);
    }
#endif
    Vsample___024root___trigger_orInto__act_vec_vec(vlSelfRef.__VnbaTriggered, vlSelfRef.__VactTriggered);
    return (0U);
}

void Vsample___024root___trigger_clear__act(VlUnpacked<QData/*63:0*/, 1> &out) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root___trigger_clear__act\n"); );
    // Locals
    IData/*31:0*/ n;
    // Body
    n = 0U;
    do {
        out[n] = 0ULL;
        n = ((IData)(1U) + n);
    } while ((1U > n));
}

bool Vsample___024root___eval_phase__nba(Vsample___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root___eval_phase__nba\n"); );
    Vsample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Locals
    CData/*0:0*/ __VnbaExecute;
    // Body
    __VnbaExecute = Vsample___024root___trigger_anySet__act(vlSelfRef.__VnbaTriggered);
    if (__VnbaExecute) {
        Vsample___024root___eval_nba(vlSelf);
        Vsample___024root___trigger_clear__act(vlSelfRef.__VnbaTriggered);
    }
    return (__VnbaExecute);
}

void Vsample___024root___eval(Vsample___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root___eval\n"); );
    Vsample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Locals
    IData/*31:0*/ __VnbaIterCount;
    // Body
    __VnbaIterCount = 0U;
    do {
        if (VL_UNLIKELY(((0x00002710U < __VnbaIterCount)))) {
#ifdef VL_DEBUG
            Vsample___024root___dump_triggers__act(vlSelfRef.__VnbaTriggered, "nba"s);
#endif
            VL_FATAL_MT("sample.v", 5, "", "DIDNOTCONVERGE: NBA region did not converge after '--converge-limit' of 10000 tries");
        }
        __VnbaIterCount = ((IData)(1U) + __VnbaIterCount);
        vlSelfRef.__VactIterCount = 0U;
        do {
            if (VL_UNLIKELY(((0x00002710U < vlSelfRef.__VactIterCount)))) {
#ifdef VL_DEBUG
                Vsample___024root___dump_triggers__act(vlSelfRef.__VactTriggered, "act"s);
#endif
                VL_FATAL_MT("sample.v", 5, "", "DIDNOTCONVERGE: Active region did not converge after '--converge-limit' of 10000 tries");
            }
            vlSelfRef.__VactIterCount = ((IData)(1U) 
                                         + vlSelfRef.__VactIterCount);
            vlSelfRef.__VactPhaseResult = Vsample___024root___eval_phase__act(vlSelf);
        } while (vlSelfRef.__VactPhaseResult);
        vlSelfRef.__VnbaPhaseResult = Vsample___024root___eval_phase__nba(vlSelf);
    } while (vlSelfRef.__VnbaPhaseResult);
}

#ifdef VL_DEBUG
void Vsample___024root___eval_debug_assertions(Vsample___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root___eval_debug_assertions\n"); );
    Vsample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    if (VL_UNLIKELY(((vlSelfRef.clk & 0xfeU)))) {
        Verilated::overWidthError("clk");
    }
    if (VL_UNLIKELY(((vlSelfRef.reset & 0xfeU)))) {
        Verilated::overWidthError("reset");
    }
}
#endif  // VL_DEBUG
