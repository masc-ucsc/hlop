// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Model implementation (design independent parts)

#include "Vsample__pch.h"
#include "verilated_vcd_c.h"

//============================================================
// Constructors

Vsample::Vsample(VerilatedContext* _vcontextp__, const char* _vcname__)
    : VerilatedModel{*_vcontextp__}
    , vlSymsp{new Vsample__Syms(contextp(), _vcname__, this)}
    , clk{vlSymsp->TOP.clk}
    , reset{vlSymsp->TOP.reset}
    , rootp{&(vlSymsp->TOP)}
{
    // Register model with the context
    contextp()->addModel(this);
    contextp()->traceBaseModelCbAdd(
        [this](VerilatedTraceBaseC* tfp, int levels, int options) { traceBaseModel(tfp, levels, options); });
}

Vsample::Vsample(const char* _vcname__)
    : Vsample(Verilated::threadContextp(), _vcname__)
{
}

//============================================================
// Destructor

Vsample::~Vsample() {
    delete vlSymsp;
}

//============================================================
// Evaluation function

#ifdef VL_DEBUG
void Vsample___024root___eval_debug_assertions(Vsample___024root* vlSelf);
#endif  // VL_DEBUG
void Vsample___024root___eval_static(Vsample___024root* vlSelf);
void Vsample___024root___eval_initial(Vsample___024root* vlSelf);
void Vsample___024root___eval_settle(Vsample___024root* vlSelf);
void Vsample___024root___eval(Vsample___024root* vlSelf);

void Vsample::eval_step() {
    VL_DEBUG_IF(VL_DBG_MSGF("+++++TOP Evaluate Vsample::eval_step\n"); );
#ifdef VL_DEBUG
    // Debug assertions
    Vsample___024root___eval_debug_assertions(&(vlSymsp->TOP));
#endif  // VL_DEBUG
    vlSymsp->__Vm_activity = true;
    vlSymsp->__Vm_deleter.deleteAll();
    if (VL_UNLIKELY(!vlSymsp->__Vm_didInit)) {
        VL_DEBUG_IF(VL_DBG_MSGF("+ Initial\n"););
        Vsample___024root___eval_static(&(vlSymsp->TOP));
        Vsample___024root___eval_initial(&(vlSymsp->TOP));
        Vsample___024root___eval_settle(&(vlSymsp->TOP));
        vlSymsp->__Vm_didInit = true;
    }
    VL_DEBUG_IF(VL_DBG_MSGF("+ Eval\n"););
    Vsample___024root___eval(&(vlSymsp->TOP));
    // Evaluate cleanup
    Verilated::endOfEval(vlSymsp->__Vm_evalMsgQp);
}

//============================================================
// Events and timing
bool Vsample::eventsPending() { return false; }

uint64_t Vsample::nextTimeSlot() {
    VL_FATAL_MT(__FILE__, __LINE__, "", "No delays in the design");
    return 0;
}

//============================================================
// Utilities

const char* Vsample::name() const {
    return vlSymsp->name();
}

//============================================================
// Invoke final blocks

void Vsample___024root___eval_final(Vsample___024root* vlSelf);

VL_ATTR_COLD void Vsample::final() {
    contextp()->executingFinal(true);
    Vsample___024root___eval_final(&(vlSymsp->TOP));
    contextp()->executingFinal(false);
}

//============================================================
// Implementations of abstract methods from VerilatedModel

const char* Vsample::hierName() const { return vlSymsp->name(); }
const char* Vsample::modelName() const { return "Vsample"; }
unsigned Vsample::threads() const { return 1; }
void Vsample::prepareClone() const { contextp()->prepareClone(); }
void Vsample::atClone() const {
    contextp()->threadPoolpOnClone();
}
std::unique_ptr<VerilatedTraceConfig> Vsample::traceConfig() const {
    return std::unique_ptr<VerilatedTraceConfig>{new VerilatedTraceConfig{false}};
};

//============================================================
// Trace configuration

void Vsample___024root__trace_decl_types(VerilatedVcd* tracep);

void Vsample___024root__trace_init_top(Vsample___024root* vlSelf, VerilatedVcd* tracep);

VL_ATTR_COLD static void trace_init(void* voidSelf, VerilatedVcd* tracep, uint32_t code) {
    // Callback from tracep->open()
    Vsample___024root* const __restrict vlSelf VL_ATTR_UNUSED = static_cast<Vsample___024root*>(voidSelf);
    Vsample__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    if (!vlSymsp->_vm_contextp__->calcUnusedSigs()) {
        VL_FATAL_MT(__FILE__, __LINE__, __FILE__,
            "Turning on wave traces requires Verilated::traceEverOn(true) call before time 0.");
    }
    vlSymsp->__Vm_baseCode = code;
    tracep->pushPrefix(vlSymsp->name(), VerilatedTracePrefixType::SCOPE_MODULE);
    Vsample___024root__trace_decl_types(tracep);
    Vsample___024root__trace_init_top(vlSelf, tracep);
    tracep->popPrefix();
}

VL_ATTR_COLD void Vsample___024root__trace_register(Vsample___024root* vlSelf, VerilatedVcd* tracep);

VL_ATTR_COLD void Vsample::traceBaseModel(VerilatedTraceBaseC* tfp, int levels, int options) {
    (void)levels; (void)options;
    VerilatedVcdC* const stfp = dynamic_cast<VerilatedVcdC*>(tfp);
    if (VL_UNLIKELY(!stfp)) {
        vl_fatal(__FILE__, __LINE__, __FILE__,"'Vsample::trace()' called on non-VerilatedVcdC object;"
            " use --trace-fst with VerilatedFst object, and --trace-vcd with VerilatedVcd object");
    }
    stfp->spTrace()->addModel(this);
    stfp->spTrace()->addInitCb(&trace_init, &(vlSymsp->TOP), name(), false, 19);
    Vsample___024root__trace_register(&(vlSymsp->TOP), stfp->spTrace());
}
