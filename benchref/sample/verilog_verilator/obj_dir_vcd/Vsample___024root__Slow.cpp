// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design implementation internals
// See Vsample.h for the primary calling header

#include "Vsample__pch.h"

void Vsample___024root___ctor_var_reset(Vsample___024root* vlSelf);

Vsample___024root::Vsample___024root(Vsample__Syms* symsp, const char* namep)
 {
    vlSymsp = symsp;
    vlNamep = strdup(namep);
    // Reset structure values
    Vsample___024root___ctor_var_reset(this);
}

void Vsample___024root::__Vconfigure(bool first) {
    (void)first;  // Prevent unused variable warning
}

Vsample___024root::~Vsample___024root() {
    VL_DO_DANGLING(std::free(const_cast<char*>(vlNamep)), vlNamep);
}
