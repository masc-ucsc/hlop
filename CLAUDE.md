hlop is the Constant node for LiveHD. Two classes share common code via **Blop**:

- **Dlop** (dynamic): runtime-sized; uses `spool_ptr` for memory pooling.
- **Slop<N>** (static): bit width is a template parameter; value type, stack allocated.

**Semantics:**
- Unknowns (base/extra pair) are always supported in Dlop. Slop uses randomization instead.
- The type system (Integer/Boolean/String/Bitwidth) applies to both — strings are just bit sequences internally.
- No mixed Dlop/Slop operations are needed, but string conversion enables cross-testing.
- C++23 semantics are fine when they yield a cleaner API or compile-time checks.
