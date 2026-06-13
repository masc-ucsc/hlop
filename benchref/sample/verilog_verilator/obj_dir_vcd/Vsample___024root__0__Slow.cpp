// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design implementation internals
// See Vsample.h for the primary calling header

#include "Vsample__pch.h"

VL_ATTR_COLD void Vsample___024root___eval_static(Vsample___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root___eval_static\n"); );
    Vsample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    vlSelfRef.__Vtrigprevexpr___TOP__clk__0 = vlSelfRef.clk;
}

VL_ATTR_COLD void Vsample___024root___eval_initial__TOP(Vsample___024root* vlSelf);
VL_ATTR_COLD void Vsample___024root____Vm_traceActivitySetAll(Vsample___024root* vlSelf);

VL_ATTR_COLD void Vsample___024root___eval_initial(Vsample___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root___eval_initial\n"); );
    Vsample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    Vsample___024root___eval_initial__TOP(vlSelf);
    Vsample___024root____Vm_traceActivitySetAll(vlSelf);
}

VL_ATTR_COLD void Vsample___024root___eval_initial__TOP(Vsample___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root___eval_initial__TOP\n"); );
    Vsample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    vlSelfRef.sample__DOT__s3__DOT__reset_iterator = 0U;
}

VL_ATTR_COLD void Vsample___024root___eval_final(Vsample___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root___eval_final\n"); );
    Vsample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
}

#ifdef VL_DEBUG
VL_ATTR_COLD void Vsample___024root___dump_triggers__stl(const VlUnpacked<QData/*63:0*/, 1> &triggers, const std::string &tag);
#endif  // VL_DEBUG
VL_ATTR_COLD bool Vsample___024root___eval_phase__stl(Vsample___024root* vlSelf);

VL_ATTR_COLD void Vsample___024root___eval_settle(Vsample___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root___eval_settle\n"); );
    Vsample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Locals
    IData/*31:0*/ __VstlIterCount;
    // Body
    __VstlIterCount = 0U;
    vlSelfRef.__VstlFirstIteration = 1U;
    do {
        if (VL_UNLIKELY(((0x00002710U < __VstlIterCount)))) {
#ifdef VL_DEBUG
            Vsample___024root___dump_triggers__stl(vlSelfRef.__VstlTriggered, "stl"s);
#endif
            VL_FATAL_MT("sample.v", 5, "", "DIDNOTCONVERGE: Settle region did not converge after '--converge-limit' of 10000 tries");
        }
        __VstlIterCount = ((IData)(1U) + __VstlIterCount);
        vlSelfRef.__VstlPhaseResult = Vsample___024root___eval_phase__stl(vlSelf);
        vlSelfRef.__VstlFirstIteration = 0U;
    } while (vlSelfRef.__VstlPhaseResult);
}

VL_ATTR_COLD void Vsample___024root___eval_triggers_vec__stl(Vsample___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root___eval_triggers_vec__stl\n"); );
    Vsample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    vlSelfRef.__VstlTriggered[0U] = ((0xfffffffffffffffeULL 
                                      & vlSelfRef.__VstlTriggered[0U]) 
                                     | (IData)((IData)(vlSelfRef.__VstlFirstIteration)));
}

VL_ATTR_COLD bool Vsample___024root___trigger_anySet__stl(const VlUnpacked<QData/*63:0*/, 1> &in);

#ifdef VL_DEBUG
VL_ATTR_COLD void Vsample___024root___dump_triggers__stl(const VlUnpacked<QData/*63:0*/, 1> &triggers, const std::string &tag) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root___dump_triggers__stl\n"); );
    // Body
    if ((1U & (~ (IData)(Vsample___024root___trigger_anySet__stl(triggers))))) {
        VL_DBG_MSGS("         No '" + tag + "' region triggers active\n");
    }
    if ((1U & (IData)(triggers[0U]))) {
        VL_DBG_MSGS("         '" + tag + "' region trigger index 0 is active: Internal 'stl' trigger - first iteration\n");
    }
}
#endif  // VL_DEBUG

VL_ATTR_COLD bool Vsample___024root___trigger_anySet__stl(const VlUnpacked<QData/*63:0*/, 1> &in) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root___trigger_anySet__stl\n"); );
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

VL_ATTR_COLD void Vsample___024root___stl_sequent__TOP__0(Vsample___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root___stl_sequent__TOP__0\n"); );
    Vsample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    vlSelfRef.sample__DOT__s2__DOT__to1_aValid = vlSelfRef.sample__DOT__to1_aValid;
    vlSelfRef.sample__DOT__s2__DOT__to1_a = vlSelfRef.sample__DOT__to1_a;
    vlSelfRef.sample__DOT__s3__DOT__to1_b = vlSelfRef.sample__DOT__to1_b;
    vlSelfRef.sample__DOT__s1__DOT__to2_aValid = vlSelfRef.sample__DOT__to2_aValid;
    vlSelfRef.sample__DOT__s1__DOT__to2_a = vlSelfRef.sample__DOT__to2_a;
    vlSelfRef.sample__DOT__s1__DOT__to2_b = vlSelfRef.sample__DOT__to2_b;
    vlSelfRef.sample__DOT__s1__DOT__to3_cValid = vlSelfRef.sample__DOT__to3_cValid;
    vlSelfRef.sample__DOT__s1__DOT__to3_c = vlSelfRef.sample__DOT__to3_c;
    vlSelfRef.sample__DOT__s2__DOT__to2_eValid = vlSelfRef.sample__DOT__to2_eValid;
    vlSelfRef.sample__DOT__s2__DOT__to2_e = vlSelfRef.sample__DOT__to2_e;
    vlSelfRef.sample__DOT__s2__DOT__to3_dValid = vlSelfRef.sample__DOT__to3_dValid;
    vlSelfRef.sample__DOT__s2__DOT__to3_d = vlSelfRef.sample__DOT__to3_d;
}

VL_ATTR_COLD void Vsample___024root___eval_stl(Vsample___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root___eval_stl\n"); );
    Vsample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    if ((1ULL & vlSelfRef.__VstlTriggered[0U])) {
        Vsample___024root___stl_sequent__TOP__0(vlSelf);
    }
}

VL_ATTR_COLD bool Vsample___024root___eval_phase__stl(Vsample___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root___eval_phase__stl\n"); );
    Vsample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Locals
    CData/*0:0*/ __VstlExecute;
    // Body
    Vsample___024root___eval_triggers_vec__stl(vlSelf);
#ifdef VL_DEBUG
    if (VL_UNLIKELY(vlSymsp->_vm_contextp__->debug())) {
        Vsample___024root___dump_triggers__stl(vlSelfRef.__VstlTriggered, "stl"s);
    }
#endif
    __VstlExecute = Vsample___024root___trigger_anySet__stl(vlSelfRef.__VstlTriggered);
    if (__VstlExecute) {
        Vsample___024root___eval_stl(vlSelf);
    }
    return (__VstlExecute);
}

bool Vsample___024root___trigger_anySet__act(const VlUnpacked<QData/*63:0*/, 1> &in);

#ifdef VL_DEBUG
VL_ATTR_COLD void Vsample___024root___dump_triggers__act(const VlUnpacked<QData/*63:0*/, 1> &triggers, const std::string &tag) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root___dump_triggers__act\n"); );
    // Body
    if ((1U & (~ (IData)(Vsample___024root___trigger_anySet__act(triggers))))) {
        VL_DBG_MSGS("         No '" + tag + "' region triggers active\n");
    }
    if ((1U & (IData)(triggers[0U]))) {
        VL_DBG_MSGS("         '" + tag + "' region trigger index 0 is active: @(posedge clk)\n");
    }
}
#endif  // VL_DEBUG

VL_ATTR_COLD void Vsample___024root____Vm_traceActivitySetAll(Vsample___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root____Vm_traceActivitySetAll\n"); );
    Vsample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    vlSelfRef.__Vm_traceActivity[0U] = 1U;
    vlSelfRef.__Vm_traceActivity[1U] = 1U;
}

VL_ATTR_COLD void Vsample___024root___ctor_var_reset(Vsample___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vsample___024root___ctor_var_reset\n"); );
    Vsample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    const uint64_t __VscopeHash = VL_MURMUR64_HASH(vlSelf->vlNamep);
    vlSelf->clk = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 16707436170211756652ull);
    vlSelf->reset = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 9928399931838511862ull);
    vlSelf->sample__DOT__to2_aValid = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 9864144566138671761ull);
    vlSelf->sample__DOT__to2_a = VL_SCOPED_RAND_RESET_I(32, __VscopeHash, 7709845022999259247ull);
    vlSelf->sample__DOT__to2_b = VL_SCOPED_RAND_RESET_I(32, __VscopeHash, 18400319978363618905ull);
    vlSelf->sample__DOT__to3_cValid = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 6794272131702921829ull);
    vlSelf->sample__DOT__to3_c = VL_SCOPED_RAND_RESET_I(32, __VscopeHash, 12890291739132625141ull);
    vlSelf->sample__DOT__to1_aValid = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 4978173050279972690ull);
    vlSelf->sample__DOT__to1_a = VL_SCOPED_RAND_RESET_I(32, __VscopeHash, 252181072282693703ull);
    vlSelf->sample__DOT__to2_eValid = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 1757864096913548255ull);
    vlSelf->sample__DOT__to2_e = VL_SCOPED_RAND_RESET_I(32, __VscopeHash, 12883519795229327371ull);
    vlSelf->sample__DOT__to3_dValid = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 14305939307530343138ull);
    vlSelf->sample__DOT__to3_d = VL_SCOPED_RAND_RESET_I(32, __VscopeHash, 18358893858729803935ull);
    vlSelf->sample__DOT__to1_b = VL_SCOPED_RAND_RESET_I(32, __VscopeHash, 10801403725370709324ull);
    vlSelf->sample__DOT__s1__DOT__to2_aValid = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 3974963556704922289ull);
    vlSelf->sample__DOT__s1__DOT__to2_a = VL_SCOPED_RAND_RESET_I(32, __VscopeHash, 4321615644933761498ull);
    vlSelf->sample__DOT__s1__DOT__to2_b = VL_SCOPED_RAND_RESET_I(32, __VscopeHash, 15467869410250993243ull);
    vlSelf->sample__DOT__s1__DOT__to3_cValid = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 10047536614597227721ull);
    vlSelf->sample__DOT__s1__DOT__to3_c = VL_SCOPED_RAND_RESET_I(32, __VscopeHash, 18091036294080714648ull);
    vlSelf->sample__DOT__s1__DOT__tmp = VL_SCOPED_RAND_RESET_I(32, __VscopeHash, 8600165368567976139ull);
    vlSelf->sample__DOT__s2__DOT__to1_aValid = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 7371385561951560279ull);
    vlSelf->sample__DOT__s2__DOT__to1_a = VL_SCOPED_RAND_RESET_I(32, __VscopeHash, 1340324866026834150ull);
    vlSelf->sample__DOT__s2__DOT__to2_eValid = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 10039499933668310331ull);
    vlSelf->sample__DOT__s2__DOT__to2_e = VL_SCOPED_RAND_RESET_I(32, __VscopeHash, 11210437634470956018ull);
    vlSelf->sample__DOT__s2__DOT__to3_dValid = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 12818179470017738223ull);
    vlSelf->sample__DOT__s2__DOT__to3_d = VL_SCOPED_RAND_RESET_I(32, __VscopeHash, 17170637322978885091ull);
    vlSelf->sample__DOT__s2__DOT__tmp = VL_SCOPED_RAND_RESET_I(32, __VscopeHash, 9721848872045238066ull);
    vlSelf->sample__DOT__s3__DOT__to1_b = VL_SCOPED_RAND_RESET_I(32, __VscopeHash, 10691928690800959927ull);
    vlSelf->sample__DOT__s3__DOT__tmp = VL_SCOPED_RAND_RESET_I(32, __VscopeHash, 9898014255359889436ull);
    vlSelf->sample__DOT__s3__DOT__tmp2 = VL_SCOPED_RAND_RESET_I(32, __VscopeHash, 9109444785747593943ull);
    for (int __Vi0 = 0; __Vi0 < 256; ++__Vi0) {
        vlSelf->sample__DOT__s3__DOT__memory[__Vi0] = VL_SCOPED_RAND_RESET_I(32, __VscopeHash, 13027095558815738175ull);
    }
    vlSelf->sample__DOT__s3__DOT__reset_iterator = VL_SCOPED_RAND_RESET_I(8, __VscopeHash, 8153622607678149689ull);
    for (int __Vi0 = 0; __Vi0 < 1; ++__Vi0) {
        vlSelf->__VstlTriggered[__Vi0] = 0;
    }
    for (int __Vi0 = 0; __Vi0 < 1; ++__Vi0) {
        vlSelf->__VactTriggered[__Vi0] = 0;
    }
    vlSelf->__Vtrigprevexpr___TOP__clk__0 = 0;
    for (int __Vi0 = 0; __Vi0 < 1; ++__Vi0) {
        vlSelf->__VnbaTriggered[__Vi0] = 0;
    }
    for (int __Vi0 = 0; __Vi0 < 2; ++__Vi0) {
        vlSelf->__Vm_traceActivity[__Vi0] = 0;
    }
}
