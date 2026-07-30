// TEMP refute probe (delete me): Dlop unknown / lane-mask behavior at >= 64 bits
#include <cstdio>

#include "dlop.hpp"
#include "memory.hpp"
#include "slop.hpp"

using namespace hlop;

static void dump_unk(const char* tag, const spool_ptr<Dlop>& v, int nbits) {
  printf("%-34s val=%-30s unk[", tag, v->to_pyrope().c_str());
  for (int b = nbits - 1; b >= 0; --b) {
    printf("%c", v->unknown_bit_test(b) ? 'x' : (v->bit_test(b) ? '1' : '0'));
  }
  int cnt = 0;
  for (int b = 0; b < nbits; ++b) {
    if (v->unknown_bit_test(b)) {
      ++cnt;
    }
  }
  printf("]  n_unknown(%d bits)=%d  has_unknowns=%d\n", nbits, cnt, (int)v->has_unknowns());
}

int main() {
  // ---- 1. Mem_val ones() at the 64-bit boundary ----------------------------
  for (int n : {8, 16, 32, 63, 64, 65, 128}) {
    auto o = Dlop::create_integer(-1)->adjust_bits(n);
    printf("dlop ones(%3d) = %-24s get_bits=%d is_neg=%d\n", n, o->to_pyrope().c_str(), o->get_bits(), (int)o->is_negative());
  }

  // ---- 2. make_unknown_bits with the ones() mask ---------------------------
  for (int n : {8, 16, 63, 64, 65, 128}) {
    auto mask = Dlop::create_integer(-1)->adjust_bits(n);
    auto stored = Dlop::create_integer(0);
    auto r      = stored->make_unknown_bits(*mask);
    char tag[64];
    snprintf(tag, sizeof(tag), "make_unknown_bits(ones(%d))", n);
    dump_unk(tag, r, n > 68 ? 68 : n);
  }

  // ---- 3. Dlop Mem_dyn ordering=none, wensize=1, several widths ------------
  for (int bits : {8, 32, 64, 128}) {
    Mem_cfg cfg;
    cfg.size = 8;
    cfg.bits = bits;
    cfg.n_rd = 1;
    cfg.n_wr = 1;
    cfg.n_user_wr = 1;
    cfg.wensize   = 1;
    cfg.order     = Mem_order::none;
    Mem_dyn<spool_ptr<Dlop>> m;
    m.configure(cfg, Dlop::create_integer(0));
    m.entries()[3] = Dlop::create_integer(7);
    m.stage_write(0, Dlop::create_bool(true), Dlop::create_integer(3), Dlop::create_integer(42));
    auto v = m.read(0, Dlop::create_integer(3));
    char tag[64];
    snprintf(tag, sizeof(tag), "none full-entry bits=%d", bits);
    dump_unk(tag, v, bits > 68 ? 68 : bits);
  }

  // ---- 4. Dlop Mem_dyn ordering=none, wensize=2, lane_bits = 32/64 --------
  for (int bits : {64, 128}) {
    Mem_cfg cfg;
    cfg.size      = 8;
    cfg.bits      = bits;
    cfg.n_rd      = 1;
    cfg.n_wr      = 1;
    cfg.n_user_wr = 1;
    cfg.wensize   = 2;
    cfg.order     = Mem_order::none;
    Mem_dyn<spool_ptr<Dlop>> m;
    m.configure(cfg, Dlop::create_integer(0));
    m.entries()[2] = Dlop::from_pyrope("0x1111_2222_3333_4444");
    m.stage_write(0, Dlop::create_integer(0b01), Dlop::create_integer(2), Dlop::create_integer(0x5555));
    auto v = m.read(0, Dlop::create_integer(2));
    char tag[64];
    snprintf(tag, sizeof(tag), "none lane0 bits=%d lane_bits=%d", bits, bits / 2);
    dump_unk(tag, v, bits > 68 ? 68 : bits);
  }

  // ---- 5. Slop reference for the same shapes ------------------------------
  {
    hlop_set_random_seed(0x1234);
    Memory_none<Slop<64>, 64, 8, 1, 1, 1, 2> s;
    s.entries()[2] = Slop<64>::from_pyrope("0x1111_2222_3333_4444");
    s.stage_write<0>(Slop<2>::create_integer(0b01), Slop<8>::create_integer(2), Slop<64>::create_integer(0x5555));
    for (int i = 0; i < 3; ++i) {
      printf("slop<64> none lane0 read = %s\n", s.read<0>(Slop<8>::create_integer(2)).to_pyrope().c_str());
    }
  }

  // ---- 6. Dlop static Mem_base with spool_ptr<Dlop> (does it compile/run?) --
  {
    Memory_none<spool_ptr<Dlop>, 64, 8, 1, 1, 1, 1> sm;
    sm.fill(Dlop::create_integer(0));
    sm.entries()[1] = Dlop::create_integer(9);
    sm.stage_write<0>(Dlop::create_bool(true), Dlop::create_integer(1), Dlop::create_integer(5));
    auto v = sm.read<0>(Dlop::create_integer(1));
    dump_unk("Mem_base<spool_ptr<Dlop>,64> none", v, 64);
  }

  return 0;
}
