//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// slop: Static Logic Operation
//
// Template class where bit-width N is known at compile time.
// Stack-allocated value type — no dynamic memory allocation.
// Uses Blop primitives for all operations.
// Slop is the runtime-only kernel: every bit is concrete. Unknown source
// bits ('?'/'x'/'z') in pyrope/binary literals are resolved to random
// concrete bits at parse time via a deterministic per-process PRNG. Ops
// never propagate unknowns and assert on Nil inputs. For Dlop-style three-
// valued semantics over symbolic unknowns, use Dlop instead.

#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <print>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "blop.hpp"
#include "iassert.hpp"

template <int N>
class Slop {
  static_assert(N >= 1, "Slop bit width must be >= 1");

  // Every Slop<M> is a friend so the cross-width converting constructor below
  // can read another width's private base_/type_/n_words.
  template <int>
  friend class Slop;

  static constexpr int n_words = (N + 63) / 64;

  enum class Type : int16_t {
    Invalid  = -1,
    Integer  = 0,
    Boolean  = 1,
    String   = 2,
    Bitwidth = 3,
    // Nil is Pyrope's tagged unit ("absence of value"). Slop intentionally does
    // NOT propagate Nil through ops at runtime — every op asserts that no input
    // is Nil. A Nil reaching an op is a caller bug, not a representable result.
    Nil      = 4
  };

  Type                         type_;
  std::array<int64_t, n_words> base_;

  constexpr Slop(Type tp, std::array<int64_t, n_words> b) : type_(tp), base_(b) {}

  static constexpr std::array<int64_t, n_words> zero_array() {
    std::array<int64_t, n_words> a{};
    for (int i = 0; i < n_words; ++i) {
      a[i] = 0;
    }
    return a;
  }

  static constexpr std::array<int64_t, n_words> fill_array(int64_t v) {
    std::array<int64_t, n_words> a{};
    a[0]         = v;
    int64_t fill = v < 0 ? -1 : 0;
    for (int i = 1; i < n_words; ++i) {
      a[i] = fill;
    }
    return a;
  }

  // Slop ops are not defined over Nil. Every binary/unary op asserts this on
  // entry — Nil reaching an op is a caller bug. Compiles out under NDEBUG, so
  // the hot path pays nothing in release builds.
  constexpr void nil_check_() const { I(type_ != Type::Nil); }
  constexpr void nil_check_(const Slop& other) const {
    I(type_ != Type::Nil);
    I(other.type_ != Type::Nil);
  }

  // Lookup tables (same as Dlop)
  constexpr static int char_to_bits[256]
      = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4, 4, 4, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  constexpr static int char_to_val[256]
      = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
         -1, -1, -1, -1, -1, -1, -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

public:
  // --- Constructors ---
  constexpr Slop() : type_(Type::Integer), base_(zero_array()) {}

  constexpr Slop(int64_t val) : type_(Type::Integer), base_(fill_array(val)) {}

  // Cross-width conversion: build a Slop<N> from a Slop<M> of a DIFFERENT width
  // (the non-template copy constructor still wins when M == N). Value-preserving
  // signed reinterpretation — the source is read as a signed M-bit quantity and
  // re-expressed as a canonical signed N-bit value:
  //   widen  (N >= M): sign-extend from bit M-1
  //   narrow (N <  M): two's-complement truncate to the low N bits, then
  //                    sign-extend from bit N-1
  // i.e. sign-extend from bit min(M,N)-1 after copying the overlapping words.
  // This is the canonicalization point cgen.sim (inou.cgen.sim) relies on: every
  // operand is wrapped Slop<Wresult>{op} before a same-width Slop op, so
  // signed-sensitive ops (SRA, signed compares, Div) always see a clean sign.
  // Not constexpr: Blop::sext is a runtime helper, and codegen only converts
  // runtime values (constants use create_integer/from_pyrope at the target width).
  template <int M>
  explicit Slop(const Slop<M>& src)
      // Type is a per-instantiation nested enum (Slop<M>::Type != Slop<N>::Type),
      // but the enumerators are identical, so round-trip through the int16_t base.
      : type_(static_cast<Type>(static_cast<int16_t>(src.type_))), base_(zero_array()) {
    constexpr int cw = (Slop<M>::n_words < n_words) ? Slop<M>::n_words : n_words;
    for (int i = 0; i < cw; ++i) {
      base_[i] = src.base_[i];
    }
    constexpr int sign_bit = (M < N ? M : N) - 1;
    Blop::sext<n_words>(base_, base_, sign_bit);
  }

  // Unsigned width change: reinterpret this width-N value as an UNSIGNED N-bit
  // quantity and re-express it at width W — keep the low min(N,W) bits, zero
  // every bit above (zero-extend on widen, mask on narrow). The companion to the
  // signed converting constructor above: cgen.sim picks per is_unsign(pin) — op
  // outputs are tagged unsigned by tolg's bind_result, so this is the common
  // path. Like the constructor, it also enforces the SOURCE's declared width on
  // read (a value an unmasked op left wider than N is wrapped to N bits here).
  template <int W>
  Slop<W> zext_to() const {
    Slop<W>       r;  // default ctor: Integer, zeroed
    constexpr int cw = (n_words < Slop<W>::n_words) ? n_words : Slop<W>::n_words;
    for (int i = 0; i < cw; ++i) {
      r.base_[i] = base_[i];
    }
    constexpr int keep = (N < W ? N : W);
    return r.adjust_bits(keep);  // clears bits >= keep -> zero-extended / masked
  }

  // --- Factory methods ---
  static constexpr Slop create_bool(bool val) { return Slop(Type::Boolean, fill_array(val ? -1 : 0)); }

  static constexpr Slop create_integer(int64_t val) { return Slop(Type::Integer, fill_array(val)); }

  static constexpr Slop create_string(std::string_view txt) {
    Slop s(Type::String, zero_array());
    for (int i = txt.size() - 1; i >= 0; --i) {
      Blop::shl<n_words>(s.base_, s.base_, 8);
      s.base_[0] |= static_cast<unsigned char>(txt[i]);
    }
    return s;
  }

  static constexpr Slop from_string(std::string_view txt) { return create_string(txt); }

  static constexpr Slop invalid() { return Slop(Type::Invalid, zero_array()); }

  static constexpr Slop nil() { return Slop(Type::Nil, zero_array()); }

  // ref shares the Invalid tag with `invalid()` — distinguished by carrying
  // a non-zero byte-packed payload. Mirrors Lconst::from_ref encoding.
  static constexpr Slop from_ref(std::string_view txt) {
    Slop s(Type::Invalid, zero_array());
    for (int i = txt.size() - 1; i >= 0; --i) {
      Blop::shl<n_words>(s.base_, s.base_, 8);
      s.base_[0] |= static_cast<unsigned char>(txt[i]);
    }
    return s;
  }

  // unknown(nbits): factory exposed for eval.hpp template compatibility. Slop
  // has no symbolic unknowns, so this returns an nbits-wide random concrete
  // value drawn from the same PRNG used at parse time. Besides the eval.hpp
  // kernels (where the `has_unknowns()` guards make it dead code) this is the
  // `ordering="none"` collision value: hlop/memory.hpp Memory_none and the
  // cgen.sim code it emits.
  //
  // The result is CANONICAL — sign-extended from bit nbits-1, not
  // zero-extended. A Slop is a signed nbits value everywhere else, and a
  // non-canonical one misbehaves under SRA, signed compare and word-wise
  // equality.
  static Slop unknown(int nbits) {
    Slop s;
    if (nbits <= 0) {
      return s;
    }
    int words = std::min((nbits + 63) / 64, n_words);
    for (int i = 0; i < words; ++i) {
      static thread_local std::mt19937_64 rng{hlop_random_seed()};
      ++hlop_random_draws();
      s.base_[i] = static_cast<int64_t>(rng());
    }
    // sext also clears every bit above the sign, so no separate mask is needed
    // (the old masked form was UB at nbits%64 == 63: int64_t(1) << 63).
    Blop::sext<n_words>(s.base_, s.base_, std::min(nbits, N) - 1);
    return s;
  }

  // Replace the bits selected by `mask` with fresh PRNG bits, keeping the rest.
  // The Slop counterpart of Dlop::make_unknown_bits — Slop has no x, so an
  // "undefined lane" is a random lane. Used by hlop/memory.hpp when a sub-word
  // (`wensize`) write collides with only some lanes of an `ordering="none"`
  // read; the untouched lanes must still read the stored value.
  Slop unknown_lanes(const Slop& mask) const {
    const Slop rnd  = unknown(N);
    const Slop kept = and_op(mask.not_op());
    const Slop got  = rnd.and_op(mask);
    return kept.or_op(got).sext_op(N - 1);
  }

  // Draw a single random bit from a deterministic per-process PRNG. Runtime
  // only — constant-evaluated calls hit the constexpr branch below and throw
  // (a '?' in a compile-time literal is not meaningful since the result would
  // be non-deterministic). Slop has no notion of unknowns at runtime, so we
  // randomize each '?'/'x'/'z' bit at parse time and store 0/1 in base_.
  static int random_bit_() {
    static thread_local std::mt19937_64 rng{hlop_random_seed()};
    ++hlop_random_draws();
    return static_cast<int>(rng() & 1);
  }

  static constexpr Slop from_binary(std::string_view txt, bool unsigned_result) {
    Slop s;
    if (!unsigned_result) {
      for (size_t i = 0; i < txt.size(); ++i) {
        auto ch = txt[i];
        if (ch == '_') {
          continue;
        }
        if (ch == '1') {
          for (int w = 0; w < n_words; ++w) {
            s.base_[w] = -1;
          }
        } else if (ch == '?' || ch == 'x' || ch == 'z') {
          if (std::is_constant_evaluated()) {
            throw std::runtime_error("ERROR: '?' in binary literal cannot be constant-evaluated");
          } else if (random_bit_()) {
            for (int w = 0; w < n_words; ++w) {
              s.base_[w] = -1;
            }
          }
        }
        break;
      }
    }

    for (size_t i = 0; i < txt.size(); ++i) {
      auto ch = txt[i];
      if (ch == '_') {
        continue;
      }

      Blop::shl<n_words>(s.base_, s.base_, 1);
      if (ch == '?' || ch == 'x' || ch == 'z') {
        if (std::is_constant_evaluated()) {
          throw std::runtime_error("ERROR: '?' in binary literal cannot be constant-evaluated");
        } else if (random_bit_()) {
          s.base_[0] |= 1;
        }
      } else if (ch == '1') {
        s.base_[0] |= 1;
      } else if (ch == '0') {
        // nothing
      } else {
        throw std::runtime_error(std::format("ERROR: {} binary encoding could not use {}\n", txt, ch));
      }
    }
    return s;
  }

  // from_pyrope is constexpr so simple compile-time literals like
  // `Slop<8>::from_pyrope("3")` fold at compile time. This avoids the
  // runtime `std::tolower` / `std::isdigit` / `std::string` work that the
  // previous implementation needed. The error paths still throw — that is
  // legal in a constexpr function as long as a constant-evaluation never
  // reaches them. Quoted-string paths produce String-typed Slops at
  // compile time too.
  static constexpr Slop from_pyrope(std::string_view orig_txt) {
    if (orig_txt.empty()) {
      return invalid();
    }

    // Manual case-insensitive equality keeps this constexpr (std::tolower
    // is locale-aware and not constant-evaluable).
    auto eq_ci = [](std::string_view a, std::string_view b) constexpr {
      if (a.size() != b.size()) {
        return false;
      }
      for (size_t i = 0; i < a.size(); ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') {
          ca = static_cast<char>(ca - 'A' + 'a');
        }
        if (ca != cb) {
          return false;
        }
      }
      return true;
    };
    auto lower        = [](char c) constexpr -> char { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
    auto is_dec_digit = [](char c) constexpr -> bool { return c >= '0' && c <= '9'; };

    if (eq_ci(orig_txt, "true")) {
      return create_bool(true);
    }
    if (eq_ci(orig_txt, "false")) {
      return create_bool(false);
    }
    // Pyrope `nil` / `null` literals parse to Type::Nil (matches Dlop). The
    // *string* "nil" must be written in quoted form `'nil'`.
    if (eq_ci(orig_txt, "nil") || eq_ci(orig_txt, "null")) {
      return nil();
    }

    bool   negative   = false;
    size_t skip_chars = 0;

    if (orig_txt.front() == '-') {
      negative   = true;
      skip_chars = 1;
    } else if (orig_txt.front() == '+') {
      skip_chars = 1;
    }

    int  shift_mode      = 0;
    bool unsigned_result = false;

    if (orig_txt.size() >= (1 + skip_chars) && is_dec_digit(orig_txt[skip_chars])) {
      shift_mode = 10;
      if (orig_txt.size() >= (2 + skip_chars) && orig_txt[skip_chars] == '0') {
        ++skip_chars;
        char sel_ch = lower(orig_txt[skip_chars]);
        if (sel_ch == 's') {
          // Signed literals are binary only: the prefix must be the full `0sb…`.
          // Bounds-check before reading the base char so a bare `0s` does not
          // read past the string_view.
          ++skip_chars;
          if (skip_chars >= orig_txt.size() || lower(orig_txt[skip_chars]) != 'b') {
            throw std::runtime_error("ERROR: unknown pyrope encoding (only 0sb...)");
          }
          sel_ch = 'b';
        } else if (sel_ch == 'u') {
          // Explicit `0u` prefix: unsigned, followed by a base selector
          // (x/b/d/o) — the binary form is `0ub…`. Bounds-check so a bare `0u`
          // does not read past the string_view.
          ++skip_chars;
          if (skip_chars >= orig_txt.size()) {
            throw std::runtime_error("ERROR: unknown pyrope encoding, use 0ub... or 0ux/0ud/0uo");
          }
          sel_ch          = lower(orig_txt[skip_chars]);
          unsigned_result = true;
        } else {
          // No explicit sign. A binary literal MUST be explicit about its
          // signedness — `0b…` is rejected; use `0ub…` (unsigned) or `0sb…`
          // (signed). Other bases stay unsigned by default.
          if (sel_ch == 'b') {
            throw std::runtime_error("ERROR: binary literal needs an explicit sign: use 0ub... or 0sb...");
          }
          unsigned_result = true;
        }

        if (sel_ch == 'x') {
          shift_mode = 4;
          ++skip_chars;
        } else if (sel_ch == 'b') {
          shift_mode = 1;
          ++skip_chars;
        } else if (sel_ch == 'd') {
          shift_mode = 10;
          ++skip_chars;
        } else if (is_dec_digit(sel_ch)) {
          shift_mode = 10;
        } else if (sel_ch == 'o') {
          shift_mode = 3;
          ++skip_chars;
        } else {
          throw std::runtime_error("ERROR: unknown pyrope encoding (leading)");
        }
      }
    } else {
      // Non-digit start → quoted/unquoted string literal.
      size_t start_i = orig_txt.size();
      size_t end_i   = 0;
      if (orig_txt.size() > 1 && orig_txt.front() == '\'' && orig_txt.back() == '\'') {
        --start_i;
        ++end_i;
      }
      return create_string(orig_txt.substr(end_i, start_i - end_i));
    }

    Slop result;

    if (shift_mode == 10) {
      for (size_t i = skip_chars; i < orig_txt.size(); ++i) {
        char c = lower(orig_txt[i]);
        int  v = char_to_val[static_cast<uint8_t>(c)];
        if (v >= 0 && v < 10) {
          std::array<int64_t, n_words> tmp_base = result.base_;
          Blop::mult<n_words>(result.base_, tmp_base, fill_array(10));
          std::array<int64_t, n_words> tmp = fill_array(v);
          Blop::add<n_words>(result.base_, result.base_, tmp);
        } else if (c == '_') {
          continue;
        } else {
          throw std::runtime_error("ERROR: invalid digit in decimal");
        }
      }
    } else if (shift_mode == 1) {
      result = from_binary(orig_txt.substr(skip_chars), unsigned_result);
      if (negative) {
        Blop::neg<n_words>(result.base_, result.base_);
      }
      return result;
    } else {
      // octal (3) or hex (4)
      for (size_t i = skip_chars; i < orig_txt.size(); ++i) {
        char c = lower(orig_txt[i]);
        if (c == '_') {
          continue;
        }
        int v = char_to_val[static_cast<uint8_t>(c)];
        if (v < 0 || (shift_mode == 3 && v >= 8)) {
          throw std::runtime_error("ERROR: invalid digit");
        }
        Blop::shl<n_words>(result.base_, result.base_, shift_mode);
        result.base_[0] |= v;
      }
    }

    if (negative) {
      Blop::neg<n_words>(result.base_, result.base_);
    }

    return result;
  }

  // =========================================================================
  // Mixed-width ops: ONE LGraph cell -> ONE Slop op
  // =========================================================================
  // An LGraph cell is an UNBOUNDED-precision integer operation; a pin's `bits`
  // is derived metadata that only says how wide a materialization has to be to
  // hold the result (pass/bitfuzz strips `bits` and recomputes it precisely
  // because it carries no semantics). The operand widths are therefore an
  // artifact of how each input happened to be materialized, not part of the
  // math.
  //
  // The member ops below are fixed-width: `Slop<A>::add_op` takes a `Slop<A>`,
  // so a caller holding a Slop<B> operand had to convert first. That forced
  // inou/cgen/cgen_sim to emit one `.zext_to<W>()` / `Slop<W>{x}` per operand
  // read purely to satisfy C++ overload resolution -- 41% of every width
  // construct it emits, and semantically a no-op whenever the target width can
  // already hold the value.
  //
  // These statics take operands at ANY width and materialize the result at N
  // directly, so cgen emits exactly one call per cell and no conversions.
  //
  // PRECONDITION: N can hold the exact result -- the bitwidth inference that
  // runs before codegen guarantees it (upass_tolg's bind_result stamps
  // bits = magnitude+1 on every computed output). There is deliberately no
  // runtime check: the graph already proved it.
  //
  // COST: when every operand and the result fit one 64-bit word (N <= 64, the
  // dominant case) each of these is a single scalar instruction on base_[0] --
  // no masking, no temporaries, nothing to elide.

  // Sign-extend a Slop<M>'s words into this width's word array. NO masking:
  // under the precondition the source words already hold the exact value, so
  // widening is pure sign propagation into the new upper words. This is also
  // why one helper serves BOTH signed and unsigned reads -- a non-negative
  // value sign-extends to zeros, which is exactly zero-extension.
  template <int M>
  static constexpr std::array<int64_t, n_words> widen_(const Slop<M>& s) {
    std::array<int64_t, n_words> a{};
    constexpr int                cw = (Slop<M>::n_words < n_words) ? Slop<M>::n_words : n_words;
    for (int i = 0; i < cw; ++i) {
      a[i] = s.base_[i];
    }
    if constexpr (Slop<M>::n_words < n_words) {
      const int64_t fill = (s.base_[Slop<M>::n_words - 1] < 0) ? -1 : 0;
      for (int i = cw; i < n_words; ++i) {
        a[i] = fill;
      }
    }
    return a;
  }

  // True when x, y and the result all fit a single word -- the scalar fast path.
  template <int A, int B>
  static constexpr bool one_word_ = (n_words == 1 && Slop<A>::n_words == 1 && Slop<B>::n_words == 1);

  template <int A, int B>
  static Slop add_op(const Slop<A>& x, const Slop<B>& y) {
    Slop r;
    if constexpr (one_word_<A, B>) {
      r.base_[0] = x.base_[0] + y.base_[0];
    } else {
      Blop::add<n_words>(r.base_, widen_(x), widen_(y));
    }
    return r;
  }

  template <int A, int B>
  static Slop sub_op(const Slop<A>& x, const Slop<B>& y) {
    Slop r;
    if constexpr (one_word_<A, B>) {
      r.base_[0] = x.base_[0] - y.base_[0];
    } else {
      auto ay = widen_(y);
      Blop::neg<n_words>(ay, ay);
      Blop::add<n_words>(r.base_, widen_(x), ay);
    }
    return r;
  }

  template <int A, int B>
  static Slop mult_op(const Slop<A>& x, const Slop<B>& y) {
    Slop r;
    if constexpr (one_word_<A, B>) {
      r.base_[0] = x.base_[0] * y.base_[0];
    } else {
      Blop::mult<n_words>(r.base_, widen_(x), widen_(y));
    }
    return r;
  }

  template <int A, int B>
  static Slop and_op(const Slop<A>& x, const Slop<B>& y) {
    Slop r;
    if constexpr (one_word_<A, B>) {
      r.base_[0] = x.base_[0] & y.base_[0];
    } else {
      Blop::band<n_words>(r.base_, widen_(x), widen_(y));
    }
    return r;
  }

  template <int A, int B>
  static Slop or_op(const Slop<A>& x, const Slop<B>& y) {
    Slop r;
    if constexpr (one_word_<A, B>) {
      r.base_[0] = x.base_[0] | y.base_[0];
    } else {
      Blop::bor<n_words>(r.base_, widen_(x), widen_(y));
    }
    return r;
  }

  template <int A, int B>
  static Slop xor_op(const Slop<A>& x, const Slop<B>& y) {
    Slop r;
    if constexpr (one_word_<A, B>) {
      r.base_[0] = x.base_[0] ^ y.base_[0];
    } else {
      Blop::bxor<n_words>(r.base_, widen_(x), widen_(y));
    }
    return r;
  }

  template <int A>
  static Slop not_op(const Slop<A>& x) {
    Slop r;
    if constexpr (n_words == 1 && Slop<A>::n_words == 1) {
      r.base_[0] = ~x.base_[0];
    } else {
      Blop::bnot<n_words>(r.base_, widen_(x));
    }
    return r;
  }

  // Comparisons materialize a 0/1 MAGNITUDE at this width. The member forms
  // return create_bool(), whose true value is all-ones (-1) -- correct for a
  // Boolean-typed Slop, but it forced cgen to append `.zext_to<1>().zext_to<W>()`
  // to every compare (740 sites in one design) to recover the 0/1 an LGraph
  // LT/GT/EQ cell is defined to produce. These give cgen that value directly.
  template <int A, int B>
  static Slop eq_op(const Slop<A>& x, const Slop<B>& y) {
    Slop r;
    if constexpr (one_word_<A, B>) {
      r.base_[0] = (x.base_[0] == y.base_[0]) ? 1 : 0;
    } else {
      r.base_[0] = Blop::eq<n_words>(widen_(x), widen_(y)) ? 1 : 0;
    }
    return r;
  }

  template <int A, int B>
  static Slop lt_op(const Slop<A>& x, const Slop<B>& y) {
    Slop r;
    if constexpr (one_word_<A, B>) {
      r.base_[0] = (x.base_[0] < y.base_[0]) ? 1 : 0;
    } else {
      r.base_[0] = Blop::lt<n_words>(widen_(x), widen_(y)) ? 1 : 0;
    }
    return r;
  }

  template <int A, int B>
  static Slop gt_op(const Slop<A>& x, const Slop<B>& y) {
    Slop r;
    if constexpr (one_word_<A, B>) {
      r.base_[0] = (x.base_[0] > y.base_[0]) ? 1 : 0;
    } else {
      r.base_[0] = Blop::lt<n_words>(widen_(y), widen_(x)) ? 1 : 0;
    }
    return r;
  }

  // Shifts: the amount is a plain count, never a materialized Slop constant.
  // cgen previously built a full Slop<W>::create_integer(k) operand just to
  // pass a shift count (788 sites in one design, 270 of them multi-word).
  template <int A>
  static Slop shl_op(const Slop<A>& x, int64_t amount) {
    Slop r;
    if (amount == 0) {
      r.base_ = widen_(x);
      return r;
    }
    Blop::shl<n_words>(r.base_, widen_(x), amount);
    return r;
  }

  template <int A>
  static Slop sra_op(const Slop<A>& x, int64_t amount) {
    Slop r;
    if (amount == 0) {
      r.base_ = widen_(x);
      return r;
    }
    Blop::shr<n_words>(r.base_, widen_(x), amount);
    return r;
  }

  // --- Arithmetic ---
  Slop add_op(const Slop& other) const {
    nil_check_(other);
    Slop result;
    if constexpr (n_words == 1) {
      result.base_[0] = base_[0] + other.base_[0];
    } else {
      Blop::add<n_words>(result.base_, base_, other.base_);
    }
    return result;
  }

  Slop sub_op(const Slop& other) const {
    nil_check_(other);
    return add_op(other.neg_op());
  }

  static Slop sum_op(std::span<const Slop> a, std::span<const Slop> b) {
    Slop result = create_integer(0);
    for (const auto& v : a) {
      result = result.add_op(v);
    }
    for (const auto& v : b) {
      result = result.sub_op(v);
    }
    return result;
  }

  static Slop sum_op(std::initializer_list<Slop> a, std::initializer_list<Slop> b) {
    return sum_op(std::span<const Slop>(a.begin(), a.size()), std::span<const Slop>(b.begin(), b.size()));
  }

  Slop mult_op(const Slop& other) const {
    nil_check_(other);
    Slop result;
    Blop::mult<n_words>(result.base_, base_, other.base_);
    return result;
  }

  Slop div_op(const Slop& other) const {
    nil_check_(other);
    I(!other.is_known_false(), "Slop division by zero");
    Slop result;
    Blop::div<n_words>(result.base_, base_, other.base_);
    return result;
  }

  // rem_op: TRUNCATED remainder, the LGraph `rem` cell (sign follows the
  // dividend, matching C/C++ `%` -- not a floored modulo). Asserts on
  // rem-by-zero.
  Slop rem_op(const Slop& other) const {
    nil_check_(other);
    I(!other.is_known_false(), "Slop remainder by zero");
    Slop result;
    // Route through Blop::mod (even for n_words==1) so the INT64_MIN % -1
    // signed-overflow UB is guarded in one place, matching div_op.
    Blop::mod<n_words>(result.base_, base_, other.base_);
    return result;
  }

  Slop neg_op() const {
    nil_check_();
    Slop result;
    Blop::neg<n_words>(result.base_, base_);
    return result;
  }

  // --- Bitwise ---
  Slop or_op(const Slop& other) const {
    nil_check_(other);
    Slop result;
    Blop::bor<n_words>(result.base_, base_, other.base_);
    return result;
  }

  Slop and_op(const Slop& other) const {
    nil_check_(other);
    Slop result;
    Blop::band<n_words>(result.base_, base_, other.base_);
    return result;
  }

  Slop xor_op(const Slop& other) const {
    nil_check_(other);
    Slop result;
    Blop::bxor<n_words>(result.base_, base_, other.base_);
    return result;
  }

  Slop not_op() const {
    nil_check_();
    Slop result;
    Blop::bnot<n_words>(result.base_, base_);
    return result;
  }

  // --- Shift ---
  Slop shl_op(int64_t amount) const {
    nil_check_();
    if (amount == 0) {
      return *this;
    }
    Slop result;
    Blop::shl<n_words>(result.base_, base_, amount);
    return result;
  }

  Slop sra_op(int64_t amount) const {
    nil_check_();
    if (amount == 0) {
      return *this;
    }
    Slop result;
    Blop::shr<n_words>(result.base_, base_, amount);
    return result;
  }

  // Slop-typed shift wrappers — forward to the int64 form after extracting
  // the amount. Slop has no runtime unknowns.
  Slop shl_op(const Slop& amount) const { return shl_op(amount.base_[0]); }
  Slop sra_op(const Slop& amount) const { return sra_op(amount.base_[0]); }

  // --- Comparison ---
  Slop eq_op(const Slop& other) const {
    nil_check_(other);
    return create_bool(Blop::eq<n_words>(base_, other.base_));
  }

  // same_repr: structural compare of (type, base). Slop has no unknowns at
  // runtime, so this is the natural form for containers, dedup, and hashing.
  // Equivalent to is_known_eq for non-Nil values, but does not assert on Nil.
  bool same_repr(const Slop& other) const {
    if (type_ != other.type_) {
      return false;
    }
    for (int i = 0; i < n_words; ++i) {
      if (base_[i] != other.base_[i]) {
        return false;
      }
    }
    return true;
  }

  // is_known_eq: numeric equality. Asserts on Nil via eq_op. Kept for API
  // symmetry with Dlop, where unknowns can collapse to "not known equal".
  bool is_known_eq(const Slop& other) const { return eq_op(other).is_known_true(); }

  // No operator==/operator!=, and no operator</<=/>/>=: callers pick same_repr
  // (structural, Nil-safe) or is_known_eq (numeric, asserts on Nil) or
  // eq_op/lt_op/le_op/gt_op/ge_op (return a Slop bool). Mirrors the Dlop API,
  // which hides comparison operators to keep unknown-propagation semantics
  // explicit at the call site (Slop has no unknowns, but staying symmetric
  // with Dlop avoids subtle behavior changes when code migrates between them).

  // Comparison ops returning a Bool Slop. Slop has no runtime unknowns, so
  // these always produce a concrete known-true/false (unlike Dlop, which
  // collapses to a 1-bit unknown when either side has unknown bits).
  Slop lt_op(const Slop& other) const {
    nil_check_(other);
    return create_bool(Blop::lt<n_words>(base_, other.base_));
  }
  Slop le_op(const Slop& other) const {
    nil_check_(other);
    return create_bool(!Blop::lt<n_words>(other.base_, base_));
  }
  Slop gt_op(const Slop& other) const {
    nil_check_(other);
    return create_bool(Blop::lt<n_words>(other.base_, base_));
  }
  Slop ge_op(const Slop& other) const {
    nil_check_(other);
    return create_bool(!Blop::lt<n_words>(base_, other.base_));
  }

  // --- Bit manipulation ---
  Slop sext_op(int from_bit) const {
    nil_check_();
    Slop result;
    Blop::sext<n_words>(result.base_, base_, from_bit);
    return result;
  }

  // Positive contiguous mask [lo, hi) detector — the shape every packed field
  // access lowers to. Word-wise; false for a zero or gapped mask. Internal
  // (trailing underscore): the fast paths of get_mask_op / set_mask_op.
  static bool contiguous_range_(const Slop& m, int& lo, int& hi) {
    lo = -1;
    hi = -1;
    for (int w = 0; w < n_words; ++w) {
      const auto mw = static_cast<uint64_t>(m.base_[w]);
      if (mw == 0) {
        continue;
      }
      if (lo < 0) {
        lo = w * 64 + __builtin_ctzll(mw);
      }
      hi = w * 64 + 64 - __builtin_clzll(mw);
    }
    if (lo < 0) {
      return false;
    }
    for (int w = lo / 64; w <= (hi - 1) / 64; ++w) {
      uint64_t expect = ~uint64_t(0);
      if (w == lo / 64) {
        expect &= ~uint64_t(0) << (lo % 64);
      }
      if (w == (hi - 1) / 64 && (hi % 64) != 0) {
        expect &= ~uint64_t(0) >> (64 - hi % 64);
      }
      if ((static_cast<uint64_t>(m.base_[w]) & expect) != expect) {
        return false;
      }
    }
    return true;
  }

  // Bits [lo, lo+len) of x (sign-extending past its storage, like bit_test),
  // LSB-aligned into a zero-filled Slop<N>. Word-wise; the extract half of the
  // contiguous fast paths. Bits beyond this width's storage are dropped,
  // matching the per-bit loops' `word < n_words` guard.
  template <int A>
  static Slop extract_bits_(const Slop<A>& x, int lo, int len) {
    Slop result = create_integer(0);
    if (len <= 0 || lo < 0) {
      return result;
    }
    if (len > 64 * n_words) {
      len = 64 * n_words;
    }
    const uint64_t xsign = x.is_negative() ? ~uint64_t(0) : uint64_t(0);
    const auto     xword = [&](int idx) -> uint64_t {
      return (idx >= 0 && idx < Slop<A>::n_words) ? static_cast<uint64_t>(x.base_[idx]) : xsign;
    };
    const int nw = (len + 63) / 64;
    for (int w = 0; w < nw && w < n_words; ++w) {
      const int j  = lo + w * 64;
      const int wi = j / 64, sh = j % 64;
      uint64_t  v  = sh == 0 ? xword(wi) : (xword(wi) >> sh) | (xword(wi + 1) << (64 - sh));
      if (w == nw - 1 && (len % 64) != 0) {
        v &= ~uint64_t(0) >> (64 - len % 64);
      }
      result.base_[w] = static_cast<int64_t>(v);
    }
    return result;
  }

  Slop get_mask_op() const {
    nil_check_();
    if (!is_negative()) {
      return *this;
    }
    // Magnitude bit pattern: clear every bit at and above bit `nbits` so the
    // result is non-negative (matches Lconst::get_mask_op). When nbits lands on
    // a word boundary (top_bit == 0) the whole top word must be zeroed.
    //
    // Fixed-width caveat: a value occupying the full width (get_bits() == N) has
    // no spare bit for the cleared sign, so its magnitude (N+1 bits) is not
    // representable in Slop<N>; top_word reaches n_words and the value is
    // returned unchanged. Dlop, being arbitrary precision, widens instead — so
    // the two intentionally diverge only at that representational boundary. Keep
    // operand widths below N (as real LiveHD values are) to avoid it.
    Slop result;
    int  nbits   = get_bits();
    result.base_ = base_;
    int top_word = nbits / 64;
    int top_bit  = nbits % 64;
    if (top_word < n_words) {
      if (top_bit == 0) {
        result.base_[top_word] = 0;
      } else {
        result.base_[top_word] &= (int64_t(1) << top_bit) - 1;
      }
    }
    for (int i = top_word + 1; i < n_words; ++i) {
      result.base_[i] = 0;
    }
    return result;
  }

  // get_mask_op(mask): copy the bits selected by `mask` into a new integer,
  // packed LSB-first in their original order. Negative mask = "everything
  // except the lowest |mask| bits". Mirrors Lconst::get_mask_op semantics.
  //
  // Single-bit result: when exactly one bit is selected, the result is the
  // signed 1-bit integer -1 (bit set) or 0 (bit clear), not 0sb01. Detected
  // from the selected-bit count after the loop — no popcount needed.
  Slop get_mask_op(const Slop& mask) const {
    nil_check_(mask);

    bool mask_neg           = mask.is_negative();
    int  mask_bits          = mask.get_bits();
    int  positive_mask_bits = mask_neg ? (mask_bits - 1) : mask_bits;
    int  src_bits           = get_bits();

    // FAST PATH — positive contiguous mask [lo, hi): the extract is a
    // word-wise shift (see set_mask_op's twin note; the per-bit walk below
    // dominated wide-datapath simulation). Keeps the member form's signed
    // single-bit quirk.
    if (!mask_neg) {
      int lo = 0, hi = 0;
      if (contiguous_range_(mask, lo, hi)) {
        if (hi - lo == 1) {
          return create_integer(bit_test(lo) ? -1 : 0);
        }
        return extract_bits_(*this, lo, hi - lo);
      }
    }

    Slop result;
    int  out_bit = 0;
    for (int i = 0; i < positive_mask_bits; ++i) {
      bool selected = mask_neg ? !mask.bit_test(i) : mask.bit_test(i);
      if (!selected) {
        continue;
      }
      // A positive mask may select bit positions ABOVE the source's minimal
      // width; those are the sign bit (0 for non-negative, 1 for negative), so
      // do NOT cap at src_bits. bit_test already sign-extends past storage, so
      // get_mask(-1, 0xff) yields 0xff rather than 1. (Slop is fixed width and
      // stores full sign extension, so no explicit is_negative() read is needed
      // like in Dlop's minimally-sized representation.)
      bool b = bit_test(i);
      if (b) {
        int word = out_bit / 64;
        int bit  = out_bit % 64;
        if (word < n_words) {
          result.base_[word] |= int64_t(1) << bit;
        }
      }
      ++out_bit;
    }
    if (mask_neg) {
      for (int i = positive_mask_bits; i < src_bits; ++i) {
        if (bit_test(i)) {
          int word = out_bit / 64;
          int bit  = out_bit % 64;
          if (word < n_words) {
            result.base_[word] |= int64_t(1) << bit;
          }
        }
        ++out_bit;
      }
    }
    if (out_bit == 1) {
      return create_integer((result.base_[0] & 1) ? -1 : 0);
    }
    return result;
  }

  // Mixed-width get_mask: ONE LGraph Get_mask cell -> ONE Slop call.
  //
  // Same bit-selection semantics as the member form above, but the value and the
  // mask come in at ANY widths and the result is materialized at N, so cgen emits
  // no per-operand conversion. On the dino CPU the member form cost TWO emitted
  // conversions per cell (operand read + result trim) -- 1039 of the 1842 that
  // survived the first 1-1 pass.
  //
  // ONE deliberate difference: a single selected bit yields the UNSIGNED 0/1,
  // not the signed -1 the member form returns. The LGraph/Pyrope Get_mask with a
  // positive mask is an unsigned LSB-first pack (`#[N]` zero-extends), and that
  // -1 is a long-standing wart every consumer patches around -- livehd clamps it
  // in five separate places (upass_constprop's get_mask_zext, cprop,
  // pass/bitwidth, cgen_verilog, cgen_sim). Producing the value the cell is
  // defined to produce lets those clamps go. The member form keeps its old
  // contract, so nothing that relies on it changes.
  template <int A, int M>
  static Slop get_mask_op(const Slop<A>& x, const Slop<M>& mask) {
    const bool mask_neg           = mask.is_negative();
    const int  mask_bits          = mask.get_bits();
    const int  positive_mask_bits = mask_neg ? (mask_bits - 1) : mask_bits;
    const int  src_bits           = x.get_bits();

    // FAST PATH — positive contiguous mask [lo, hi): a word-wise shift of x,
    // zero-filled above (the unsigned LSB-first pack this form is defined to
    // produce, single selected bit included). The per-bit walk below
    // dominated wide-datapath simulation; see set_mask_op's twin note.
    if (!mask_neg) {
      int lo = 0, hi = 0;
      if (Slop<M>::contiguous_range_(mask, lo, hi)) {
        return extract_bits_(x, lo, hi - lo);
      }
    }

    Slop result;
    int  out_bit = 0;
    auto put     = [&result](int ob) {
      const int word = ob / 64;
      if (word < n_words) {
        result.base_[word] |= int64_t(1) << (ob % 64);
      }
    };
    for (int i = 0; i < positive_mask_bits; ++i) {
      const bool selected = mask_neg ? !mask.bit_test(i) : mask.bit_test(i);
      if (!selected) {
        continue;
      }
      if (x.bit_test(i)) {  // bit_test sign-extends past storage
        put(out_bit);
      }
      ++out_bit;
    }
    if (mask_neg) {
      for (int i = positive_mask_bits; i < src_bits; ++i) {
        if (x.bit_test(i)) {
          put(out_bit);
        }
        ++out_bit;
      }
    }
    if (out_bit == 1) {
      result.base_[0] &= 1;  // unsigned 0/1, NOT the member form's signed -1
      for (int i = 1; i < n_words; ++i) {
        result.base_[i] = 0;
      }
    }
    return result;
  }

  // Mux with an already-decoded integer index. The Slop-selector form forces the
  // caller to materialize the index as a full Slop at the RESULT width, which for
  // a wide mux meant building a multi-word constant to carry a 0/1 select.
  static Slop mux_op(int64_t idx, std::span<const Slop> values) {
    assert(!values.empty());
    if (idx < 0 || static_cast<size_t>(idx) >= values.size()) {
      return invalid();
    }
    return values[idx];
  }
  static Slop mux_op(int64_t idx, std::initializer_list<Slop> values) {
    return mux_op(idx, std::span<const Slop>(values.begin(), values.size()));
  }

  // set_mask_op(mask, value): replace the bits selected by `mask` with bits
  // taken LSB-first from `value`; bits not selected stay unchanged. Mirrors
  // Lconst::set_mask_op for the non-string, non-unknown path.
  Slop set_mask_op(const Slop& mask, const Slop& value) const {
    nil_check_(mask);
    I(!value.is_nil());
    if (mask.is_known_false()) {
      return *this;
    }

    bool mask_neg           = mask.is_negative();
    int  mask_bits          = mask.get_bits();
    int  positive_mask_bits = mask_neg ? (mask_bits - 1) : mask_bits;

    int src_bits = get_bits();
    int val_bits = value.get_bits();
    int out_bits = std::max(src_bits, mask_bits);
    if (mask_neg) {
      out_bits = std::max(out_bits, positive_mask_bits + val_bits);
    }
    if (out_bits > N) {
      out_bits = N;
    }

    // FAST PATH — a positive CONTIGUOUS mask [lo, hi): the shape every packed
    // field write lowers to. The generic loop below walks EVERY bit of the
    // result; on wide datapath Slops (the 500-1000+ bit VPU vectors of
    // lhdsuite's minion) that per-bit walk dominated whole-design simulation.
    // A contiguous range is a word-wise splice: clear [lo, hi), OR in
    // `value << lo`. Bit-for-bit identical to the loop (value bits map
    // LSB-first onto the selected range; bits at/above out_bits never write
    // and never consume value bits, hence the `hi` cap).
    if (!mask_neg) {
      int lo = -1, hi = -1;
      for (int w = 0; w < n_words; ++w) {
        const auto mw = static_cast<uint64_t>(mask.base_[w]);
        if (mw == 0) {
          continue;
        }
        if (lo < 0) {
          lo = w * 64 + __builtin_ctzll(mw);
        }
        hi = w * 64 + 64 - __builtin_clzll(mw);
      }
      bool contig = lo >= 0;
      for (int w = lo / 64; contig && w <= (hi - 1) / 64; ++w) {
        uint64_t expect = ~uint64_t(0);
        if (w == lo / 64) {
          expect &= ~uint64_t(0) << (lo % 64);
        }
        if (w == (hi - 1) / 64 && (hi % 64) != 0) {
          expect &= ~uint64_t(0) >> (64 - hi % 64);
        }
        contig = (static_cast<uint64_t>(mask.base_[w]) & expect) == expect;
      }
      if (contig) {
        if (hi > out_bits) {
          hi = out_bits;
        }
        Slop fast = *this;
        if (hi > lo) {
          const uint64_t vsign = value.is_negative() ? ~uint64_t(0) : uint64_t(0);
          const auto     vword = [&](int idx) -> uint64_t {
            return (idx >= 0 && idx < n_words) ? static_cast<uint64_t>(value.base_[idx]) : vsign;
          };
          const auto vshifted = [&](int w) -> uint64_t {  // 64 bits of (value << lo) at word w
            const int j = w * 64 - lo;
            if (j >= 0) {
              const int wi = j / 64, sh = j % 64;
              return sh == 0 ? vword(wi) : (vword(wi) >> sh) | (vword(wi + 1) << (64 - sh));
            }
            return vword(0) << (-j);
          };
          for (int w = lo / 64; w <= (hi - 1) / 64 && w < n_words; ++w) {
            uint64_t m = ~uint64_t(0);
            if (w == lo / 64) {
              m &= ~uint64_t(0) << (lo % 64);
            }
            if (w == (hi - 1) / 64 && (hi % 64) != 0) {
              m &= ~uint64_t(0) >> (64 - hi % 64);
            }
            fast.base_[w] = static_cast<int64_t>((static_cast<uint64_t>(fast.base_[w]) & ~m) | (vshifted(w) & m));
          }
        }
        return fast;
      }
    }

    // Start from `this` so bits not selected by the mask — including the
    // sign-extension region beyond src_bits — carry through unchanged. The
    // loop then overwrites just the bits the mask selects.
    Slop result    = *this;
    int  value_pos = 0;
    for (int i = 0; i < out_bits; ++i) {
      bool from_value;
      if (i < positive_mask_bits) {
        bool mb    = mask.bit_test(i);
        from_value = mask_neg ? !mb : mb;
      } else {
        from_value = mask_neg;
      }
      if (!from_value) {
        continue;
      }
      bool the_bit = value.bit_test(value_pos);
      ++value_pos;
      int word = i / 64;
      int bit  = i % 64;
      if (word < n_words) {
        if (the_bit) {
          result.base_[word] |= (int64_t(1) << bit);
        } else {
          result.base_[word] &= ~(int64_t(1) << bit);
        }
      }
    }
    return result;
  }

  // ror_op: OR-reduction with another operand to a single bit (1 if either
  // side has any nonzero bit). Matches Lconst::ror_op.
  Slop ror_op(const Slop& other) const {
    nil_check_(other);
    bool any = is_known_true() || other.is_known_true();
    return Slop(any ? int64_t(1) : int64_t(0));
  }

  // ror_op (unary): OR-reduction over this operand's bits, returning a Bool
  // Slop. Slop has no runtime unknowns, so the result is always known.
  Slop ror_op() const {
    nil_check_();
    return create_bool(is_known_true());
  }

  // rand_op: AND-reduction (single operand). True iff every bit is set
  // (the value is a 2^n-1 mask).
  Slop rand_op() const {
    nil_check_();
    return create_bool(is_mask());
  }

  // rxor_op: XOR-reduction (single operand). True iff popcount is odd.
  Slop rxor_op() const {
    nil_check_();
    return create_bool((popcount() & 1) == 1);
  }

  // popcount_op: number of set bits as an Integer Slop. Slop carries no
  // unknowns, so this is always the exact count (the unknown-range encoding
  // the Dlop variant needs is not applicable here).
  Slop popcount_op() const {
    nil_check_();
    return Slop(static_cast<int64_t>(popcount()));
  }

  Slop concat_op(const Slop& other) const {
    nil_check_(other);
    int other_bits = other.get_bits();
    if (other_bits <= 0) {
      return *this;
    }
    auto shifted = shl_op(other_bits);
    auto masked  = other.get_mask_op();
    return shifted.or_op(masked);
  }

  // --- Multiplexers / LUT (computing cells from livehd graph/cell.*) ---
  // Static, with the selector/address and ordered value list passed
  // explicitly. Slop carries no unknowns, so these are the plain concrete
  // cases of the matching Dlop ops.

  // mux_op: Y = values[sel] (0-based). A non-integer or out-of-range selector
  // returns invalid().
  static Slop mux_op(const Slop& sel, std::span<const Slop> values) {
    assert(!values.empty());
    if (!sel.is_just_i64()) {
      return invalid();
    }
    int64_t idx = sel.to_just_i64();
    if (idx < 0 || static_cast<size_t>(idx) >= values.size()) {
      return invalid();
    }
    return values[idx];
  }
  static Slop mux_op(const Slop& sel, std::initializer_list<Slop> values) {
    return mux_op(sel, std::span<const Slop>(values.begin(), values.size()));
  }

  // hotmux_op: one-hot selector — bit `i` selects values[i]. The selector is
  // asserted to be one-hot; an out-of-range hot bit returns invalid().
  static Slop hotmux_op(const Slop& sel, std::span<const Slop> values) {
    assert(!values.empty());
    assert(sel.popcount() == 1 && "hotmux select must be one-hot");
    int b = sel.get_first_bit_set();
    if (b < 0 || static_cast<size_t>(b) >= values.size()) {
      return invalid();
    }
    return values[b];
  }
  static Slop hotmux_op(const Slop& sel, std::initializer_list<Slop> values) {
    return hotmux_op(sel, std::span<const Slop>(values.begin(), values.size()));
  }

  // lut_op: Yosys `$lut` semantics — 1-bit result `table[addr]` (bit `addr` of
  // the truth table, addr's LSB = first input).
  static Slop lut_op(const Slop& table, const Slop& addr) {
    if (!addr.is_just_i64()) {
      return invalid();
    }
    int64_t idx = addr.to_just_i64();
    if (idx < 0) {
      return invalid();
    }
    return create_bool(table.bit_test(static_cast<int>(idx)));
  }

  Slop adjust_bits(int amount) const {
    assert(amount > 0);
    Slop result;
    result.base_ = base_;
    int top_word = amount / 64;
    int top_bit  = amount % 64;
    if (top_word < n_words) {
      if (top_bit == 0) {
        // `amount` lands on a word boundary: the whole word (bits amount..+63)
        // must be cleared, not skipped — otherwise the high word leaks through.
        result.base_[top_word] = 0;
      } else {
        // Build the keep-mask in unsigned space: at top_bit==63, int64_t(1)<<63
        // would overflow (UB under -ftrapv/UBSan).
        result.base_[top_word] &= static_cast<int64_t>((uint64_t(1) << top_bit) - 1);
      }
    }
    for (int i = top_word + 1; i < n_words; ++i) {
      result.base_[i] = 0;
    }
    return result;
  }

  // --- Queries ---
  bool           is_negative() const { return Blop::is_negative<n_words>(base_); }
  bool           is_positive() const { return !is_negative(); }
  bool           is_known_false() const { return Blop::is_zero<n_words>(base_); }
  bool           is_known_true() const { return !Blop::is_zero<n_words>(base_); }
  // Slop never carries unknowns past parse — always false. Kept so templated
  // kernels in eval.hpp (shared with Dlop) compile; their `if (has_unknowns())`
  // branches are dead code for Slop.
  constexpr bool has_unknowns() const { return false; }
  bool           is_invalid() const { return type_ == Type::Invalid; }
  // is_ref: encoded as Type::Invalid carrying a non-zero packed-string
  // payload — mirrors Lconst::is_ref. `invalid()` (no value) has all-zero
  // base; `from_ref` keeps the same Invalid tag but stores bytes there.
  bool           is_ref() const {
    if (type_ != Type::Invalid) {
      return false;
    }
    for (int i = 0; i < n_words; ++i) {
      if (base_[i] != 0) {
        return true;
      }
    }
    return false;
  }
  bool is_integer() const { return type_ == Type::Integer; }
  bool is_string() const { return type_ == Type::String; }
  bool is_nil() const { return type_ == Type::Nil; }

  // Exact representational equality: same type tag, same stored words. Used by
  // the sim's change-gated evaluation (a compare that says "different" for
  // equal values would only cost a wasted re-settle; one that says "equal" for
  // different values would be a missed update, so this is exact, not semantic).
  bool identical(const Slop& o) const { return type_ == o.type_ && base_ == o.base_; }

  bool is_mask() const {
    if (is_negative()) {
      return false;
    }
    if constexpr (n_words == 1) {
      // Read unsigned (is_negative() already excluded negatives): base_[0]+1
      // overflows when base_[0]==INT64_MAX, a valid 2^63-1 mask. Mirrors the
      // unsigned top-word read in the multi-word branch below.
      uint64_t w = static_cast<uint64_t>(base_[0]);
      return w != 0 && ((w + 1) & w) == 0;
    } else {
      int top = n_words - 1;
      while (top > 0 && base_[top] == 0) {
        --top;
      }
      // The highest non-zero word is the top of a positive value (is_negative
      // was excluded above), so read it UNSIGNED: an all-ones low word
      // (0xFFFF…F, i.e. -1 as int64) is the valid 64-bit run of `2^64-1`, not a
      // negative word.
      uint64_t topw = static_cast<uint64_t>(base_[top]);
      if (topw == 0) {
        return false;  // the value is zero
      }
      if (((topw + 1) & topw) != 0) {
        return false;
      }
      for (int i = 0; i < top; ++i) {
        if (base_[i] != -1) {
          return false;
        }
      }
      return true;
    }
  }

  bool is_power2() const {
    if (is_negative()) {
      return false;
    }
    if constexpr (n_words == 1) {
      return base_[0] > 0 && ((base_[0] - 1) & base_[0]) == 0;
    } else {
      int nonzero_count = 0;
      int nonzero_idx   = -1;
      for (int i = 0; i < n_words; ++i) {
        if (base_[i] != 0) {
          ++nonzero_count;
          nonzero_idx = i;
        }
      }
      if (nonzero_count != 1) {
        return false;
      }
      // Read the lone non-zero word UNSIGNED: a positive value can carry its
      // single set bit at position 63 of a low word (word == 0x8000…0, i.e.
      // INT64_MIN as int64). Signed `w - 1` there is overflow UB; unsigned
      // wraps correctly.
      uint64_t w = static_cast<uint64_t>(base_[nonzero_idx]);
      return ((w - 1) & w) == 0;
    }
  }

  constexpr int get_bits() const { return Blop::get_bits<n_words>(base_); }

  bool bit_test(int pos) const {
    int word = pos / 64;
    int bit  = pos % 64;
    if (word >= n_words) {
      return base_[n_words - 1] < 0;
    }
    return (base_[word] >> bit) & 1;
  }

  int get_first_bit_set() const {
    auto c = Blop::ctz<n_words>(base_);
    return (c >= n_words * 64) ? -1 : c;
  }

  int get_last_bit_set() const { return Blop::msb<n_words>(base_); }

  int popcount() const { return Blop::popcount<n_words>(base_); }

  int get_trailing_zeroes() const {
    if (is_known_false()) {
      return 0;
    }
    return Blop::ctz<n_words>(base_);
  }

  constexpr bool is_just_i64() const { return get_bits() <= 62; }

  constexpr int64_t to_just_i64() const {
    assert(is_just_i64());
    return base_[0];
  }

  // The low 64 bits as a signed int64, WITHOUT the is_just_i64() assert — for
  // best-effort numeric inspection (sim --probe / --break-when) of a value that
  // may not fit in 62 bits. Wider signals are truncated to their low word.
  constexpr int64_t to_i64_low() const { return base_[0]; }

  // --- Conversion ---
  std::string to_string() const {
    std::string str;
    if constexpr (n_words == 1) {
      uint64_t tmp = static_cast<uint64_t>(base_[0]);
      while (tmp) {
        str.push_back(static_cast<char>(tmp & 0xFF));
        tmp >>= 8;
      }
    } else {
      for (int w = 0; w < n_words; ++w) {
        uint64_t tmp = static_cast<uint64_t>(base_[w]);
        for (int b = 0; b < 8; ++b) {
          auto ch = static_cast<char>(tmp & 0xFF);
          if (ch == 0 && w == n_words - 1) {
            break;
          }
          str.push_back(ch);
          tmp >>= 8;
        }
      }
      while (!str.empty() && str.back() == '\0') {
        str.pop_back();
      }
    }
    return str;
  }

  // '_'-group `digits` every `group` characters from the LSB end (after any
  // leading '-'), the pretty-print spacing used by to_binary/to_hex/to_decimal.
  static std::string group_digits(std::string s, int group) {
    const size_t sign = (!s.empty() && s.front() == '-') ? 1u : 0u;
    std::string  g;
    int          cnt = 0;
    for (size_t k = s.size(); k > sign; --k) {
      if (cnt != 0 && cnt % group == 0) {
        g.push_back('_');
      }
      g.push_back(s[k - 1]);
      ++cnt;
    }
    if (sign != 0) {
      g.push_back('-');
    }
    std::reverse(g.begin(), g.end());
    return g;
  }

  // Zero-pad `s` to `digits` (after any leading '-').
  static std::string pad_digits(std::string s, int digits) {
    const size_t sign = (!s.empty() && s.front() == '-') ? 1u : 0u;
    if (digits > 0 && static_cast<size_t>(digits) > s.size() - sign) {
      s.insert(sign, static_cast<size_t>(digits) - (s.size() - sign), '0');
    }
    return s;
  }

  std::string to_binary() const {  // full declared width (VCD/raw callers)
    int nbits = get_bits();
    if (nbits <= 0) {
      return "0";
    }
    std::string result;
    for (int i = nbits - 1; i >= 0; --i) {
      result.push_back(bit_test(i) ? '1' : '0');
    }
    return result;
  }

  // Display overload (string-interpolation convention): leading zeros drop,
  // then `digits` zero-pads and `sep` groups — identical to Dlop's.
  std::string to_binary(int digits, bool sep) const {
    std::string result = to_binary();
    size_t      nz     = result.find_first_not_of('0');
    if (nz == std::string::npos) {
      result = "0";
    } else if (nz > 0) {
      result.erase(0, nz);
    }
    result = pad_digits(std::move(result), digits);
    return sep ? group_digits(std::move(result), 4) : result;
  }

  // Decimal / hex renderings at ANY width (no 64-bit truncation) — the
  // standard formatting entry points for pretty-printers (e.g. the lhd sim
  // driver's `puts` interpolation). `digits` zero-pads (after any '-'); `sep`
  // inserts a '_' every 4 digits (3 for decimal) from the LSB.
  std::string to_hex(int digits = 0, bool sep = false, bool upper = false) const {
    std::string result;
    auto append_mag = [&](const Slop& mag) {
      bool lead = true;
      for (int i = n_words - 1; i >= 0; --i) {
        auto w = static_cast<uint64_t>(mag.base_[i]);
        if (lead) {
          if (w == 0 && i != 0) {
            continue;
          }
          result += std::format("{:x}", w);
          lead    = false;
        } else {
          result += std::format("{:016x}", w);
        }
      }
      if (result.empty()) {
        result = "0";
      }
    };
    if (is_negative()) {
      append_mag(neg_op());
      result.insert(0, 1, '-');
    } else {
      append_mag(*this);
    }
    if (upper) {
      for (auto& c : result) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      }
    }
    result = pad_digits(std::move(result), digits);
    return sep ? group_digits(std::move(result), 4) : result;
  }

  std::string to_decimal(int digits = 0, bool sep = false) const {
    std::string result;
    if (is_just_i64()) {
      result = std::to_string(to_just_i64());
    } else {
      // Wide bignum -> decimal: repeated limb division by 10^19 (the largest
      // power of ten in a uint64), emitting 19 digits per round.
      const bool             neg = is_negative();
      const Slop             mag = neg ? neg_op() : *this;
      std::vector<uint64_t>  limbs(mag.base_.begin(), mag.base_.end());
      constexpr uint64_t     kChunk = 10000000000000000000ULL;  // 10^19
      std::vector<uint64_t>  chunks;
      auto nonzero = [&]() {
        for (auto w : limbs) {
          if (w != 0) {
            return true;
          }
        }
        return false;
      };
      while (nonzero()) {
        unsigned __int128 rem = 0;
        for (int i = static_cast<int>(limbs.size()) - 1; i >= 0; --i) {
          unsigned __int128 cur = (rem << 64) | limbs[static_cast<size_t>(i)];
          limbs[static_cast<size_t>(i)] = static_cast<uint64_t>(cur / kChunk);
          rem                            = cur % kChunk;
        }
        chunks.push_back(static_cast<uint64_t>(rem));
      }
      if (chunks.empty()) {
        result = "0";
      } else {
        result = std::to_string(chunks.back());
        for (int i = static_cast<int>(chunks.size()) - 2; i >= 0; --i) {
          result += std::format("{:019}", chunks[static_cast<size_t>(i)]);
        }
      }
      if (neg && result != "0") {
        result.insert(0, 1, '-');
      }
    }
    result = pad_digits(std::move(result), digits);
    return sep ? group_digits(std::move(result), 3) : result;
  }

  std::string to_pyrope() const {
    if (is_invalid()) {
      return "";
    }

    if (type_ == Type::String) {
      auto str = to_string();
      if (str.empty()) {
        return "''";
      }
      return std::format("'{}'", str);
    }

    if (type_ == Type::Boolean) {
      return is_known_true() ? "true" : "false";
    }

    if (is_just_i64()) {
      int64_t val = to_just_i64();
      if (val >= -63 && val <= 63) {
        return std::to_string(val);
      }
      if (val < 0) {
        return std::format("-0x{:x}", -val);
      }
      return std::format("0x{:x}", val);
    }

    // Multi-word hex
    if (is_negative()) {
      auto        pos    = neg_op();
      std::string result = "-0x";
      for (int i = n_words - 1; i >= 0; --i) {
        if (i == n_words - 1) {
          result += std::format("{:x}", static_cast<uint64_t>(pos.base_[i]));
        } else {
          result += std::format("{:016x}", static_cast<uint64_t>(pos.base_[i]));
        }
      }
      return result;
    }

    std::string result = "0x";
    for (int i = n_words - 1; i >= 0; --i) {
      if (i == n_words - 1) {
        result += std::format("{:x}", static_cast<uint64_t>(base_[i]));
      } else {
        result += std::format("{:016x}", static_cast<uint64_t>(base_[i]));
      }
    }
    return result;
  }

  std::string to_verilog() const {
    if (is_known_false()) {
      return "'sb0";
    }
    if (type_ == Type::String) {
      return std::format("\"{}\"", to_string());
    }
    int nbits = get_bits();
    // For negatives, format the two's-complement magnitude (neg_op) as hex,
    // matching Dlop::to_verilog. Emitting the raw sign-extended words would
    // print the full-width 0xff..f instead of the magnitude.
    Slop        pos;
    const Slop* src = this;
    if (is_negative()) {
      pos = neg_op();
      src = &pos;
    }
    if (src->is_just_i64()) {
      return std::format("{}'sh{:x}", nbits, static_cast<uint64_t>(src->base_[0]));
    }
    std::string hex;
    for (int i = n_words - 1; i >= 0; --i) {
      if (i == n_words - 1) {
        hex += std::format("{:x}", static_cast<uint64_t>(src->base_[i]));
      } else {
        hex += std::format("{:016x}", static_cast<uint64_t>(src->base_[i]));
      }
    }
    return std::format("{}'sh{}", nbits, hex);
  }

  // --- Debug ---
  void dump() const {
    std::print("Slop<{}> base:0x", N);
    for (int i = n_words - 1; i >= 0; --i) {
      std::print("_{:016x}", static_cast<uint64_t>(base_[i]));
    }
    std::print("\n");
  }
};

// ── Whole-array memory helpers (used by cgen_sim) ────────────────────────────
// A whole-array memory is a std::array<Slop<B>, S> of S entries of B bits. These
// bridge it to/from the size*bits buses on the Memory cell's `read_all` (async
// whole read) and `update` (whole next-state) pins. Layout: entry 0 in the LOW
// bits, row-major — identical to cgen_verilog/pass-lec so LEC and sim agree.
//
// Pack the array into one B*S-bit bus (the async `read_all` output). zext_to
// keeps each B-bit field isolated (NOT concat_op, whose minimal-width packing
// would destroy the fixed field grid; NOT the signed ctor, which would
// sign-pollute the high bits).
template <int B, std::size_t S>
Slop<B * static_cast<int>(S)> slop_read_all(const std::array<Slop<B>, S>& a) {
  constexpr int W = B * static_cast<int>(S);
  Slop<W>       bus;  // default-constructs to integer 0
  for (std::size_t i = 0; i < S; ++i) {
    bus = bus.or_op(a[i].template zext_to<W>().shl_op(static_cast<int64_t>(i) * B));
  }
  return bus;
}

// Scatter a W-bit `update` bus into the per-entry array. The cross-width ctor
// truncates+sign-fits each slice to the canonical signed B-bit entry, matching
// how cgen_sim stores/reads entries elsewhere (so logical-vs-arith shift of the
// high field bits is irrelevant). Returns whether any entry actually changed
// (the sim's change-gated evaluation folds this into its generation counter).
template <int B, std::size_t S, int W>
bool slop_apply_update(std::array<Slop<B>, S>& dst, const Slop<W>& bus) {
  bool changed = false;
  for (std::size_t i = 0; i < S; ++i) {
    Slop<B> nv{bus.sra_op(static_cast<int64_t>(i) * B)};
    if (!dst[i].identical(nv)) {
      dst[i]  = nv;
      changed = true;
    }
  }
  return changed;
}

// Compare-on-write: assign only when the stored value differs, reporting
// whether it did. The sim's ports-as-members design funnels every input-field
// write and state commit through this, accumulating the result into the
// instance's `__gen` generation counter — the substrate for skipping settles
// of quiesced (idle / clock-gated) cones.
template <int N>
inline bool slop_update(Slop<N>& dst, const Slop<N>& v) {
  if (dst.identical(v)) {
    return false;
  }
  dst = v;
  return true;
}
inline bool slop_update(bool& dst, bool v) {
  if (dst == v) {
    return false;
  }
  dst = v;
  return true;
}
