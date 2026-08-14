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
#include <compare>
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

// Slop_u<N>: the canonical-unsigned materialization of a Slop (defined at the
// bottom of this header). Declared here so Slop can convert from one and so
// concat_op can take Slop_u lanes.
template <int N>
class Slop_u;

// Lane traits for the n-ary concat_op. A lane carries its width in its TYPE:
// `Slop<W>` contributes W raw bits (sign slot included) and needs a mask,
// because Slop storage above bit W-1 is deliberately non-canonical;
// `Slop_u<W>` contributes W bits and needs none. Declared (not defined) here:
// concat_op only names it through its template parameters, so it is looked up
// at instantiation, after the specializations below.
template <typename T>
struct Slop_lane;

// Operand traits for the mixed-width statics: the WIDTH of an operand and
// whether its storage is CANONICAL. Same declared-not-defined shape as
// Slop_lane -- the statics only name it through their template parameters, so
// it is looked up at instantiation, after the specializations below.
//
// This is what lets a Slop_u<W> be passed to Slop<N>::add_op & co. bare. It is
// not only syntax: `.raw()` hands back a Slop<W+1> whose canonicality the
// compiler cannot see, so widening it re-runs widen_'s runtime sign fill. With
// the trait, awords_ picks zero_widen_ instead.
template <typename T>
struct Slop_arg;

template <typename T>
concept Slop_operand = requires { Slop_arg<T>::bits; };

template <int N>
class Slop {
  static_assert(N >= 1, "Slop bit width must be >= 1");

  // Every Slop<M> is a friend so the cross-width converting constructor below
  // can read another width's private base_/type_/n_words.
  template <int>
  friend class Slop;

  static constexpr int n_words = (N + 63) / 64;

  enum class Type : int64_t {
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
      // but the enumerators are identical, so round-trip through the int64_t base.
      : type_(static_cast<Type>(static_cast<int64_t>(src.type_))), base_(zero_array()) {
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
  //
  // COST: everything below is decided at compile time. `keep = min(N,W)` bits
  // are copied word-by-word (the default ctor already zeroed the rest), and at
  // most ONE word ever needs masking -- the word `keep` falls inside. When
  // `keep` instead lands exactly on the top of the copied words the mask is
  // provably redundant and drops out entirely, so the whole call is a copy:
  //   * W % 64 == 0 && N >= W  (e.g. zext_to<64> of any N >= 64)
  //   * N % 64 == 0 && N <= W  (e.g. Slop<64> -> zext_to<100>)
  // The N == W case still masks whenever N % 64 != 0: Slop stores values
  // SIGN-extended and the ops do not re-mask, so bits above N-1 are routinely
  // set -- Slop<8>{-1} has all 64 bits set, and add_op at width 8 leaves 300
  // in base_[0]. Skipping that AND would turn `zext_to<8>()` into a no-op that
  // silently drops the mod-2^8 wrap cgen.sim relies on.
  // CarrierBits lets a caller keep the semantic zero-extension width while
  // choosing a wider C++ carrier. For example, zext_to<130, 131>() retains bits
  // [129:0], clears bit 130, and constructs Slop<131> in one pass; spelling
  // zext_to<130>().zext_to<131>() performed the same copy/mask work twice.
  // The default preserves the original zext_to<W>() API and semantics.
  template <int W, int CarrierBits = W>
  constexpr Slop<CarrierBits> zext_to() const {
    static_assert(W >= 0, "zext_to width must be non-negative");
    static_assert(CarrierBits >= W, "zext_to carrier is narrower than the zero-extended value");
    constexpr int  cw        = (n_words < Slop<CarrierBits>::n_words) ? n_words : Slop<CarrierBits>::n_words;
    constexpr int  keep      = (N < W ? N : W);
    constexpr int  top_word  = keep / 64;
    // Only the copied words can hold bits >= keep; anything above stays 0 from
    // the default ctor. cw <= top_word+1 always, so this is the ONLY mask.
    constexpr bool need_mask = top_word < cw;

    Slop<CarrierBits> r;  // default ctor: Integer, zeroed
    for (int i = 0; i < cw; ++i) {
      r.base_[i] = base_[i];
    }
    if constexpr (need_mask) {
      // Built in unsigned space: at keep%64 == 63, int64_t(1) << 63 would be UB.
      r.base_[top_word] &= static_cast<int64_t>((uint64_t(1) << (keep % 64)) - 1);
    }
    return r;
  }

  // Name the value zext_to already produced. zext_to<W, W+1>() IS
  // Slop_u<W>'s invariant, so this hands the masked carrier straight to the
  // private canonical factory -- one mask total, where `Slop_u<W>{x}` would
  // mask and then mask again inside the ctor.
  template <int W>
  Slop_u<W> zext_to_u() const {
    return Slop_u<W>::from_canonical_(zext_to<W, W + 1>());
  }

  // Unsigned materialization -> signed carrier. A Slop_u<M> is CANONICAL by
  // construction (every bit at and above M is already zero), so widening is a
  // pure word copy: no mask, and no sign propagation to undo -- the "free"
  // direction of the conversion pair. Narrowing (N <= M) keeps the low N bits
  // and re-canonicalizes the sign, exactly like the Slop<M> ctor above, so
  // Slop<8>{Slop_u<8>{255}} is -1 while Slop<9>{Slop_u<8>{255}} is 255.
  template <int M>
  explicit Slop(const Slop_u<M>& src) : Slop(from_canonical_(src.raw())) {
    if constexpr (N <= M) {
      Blop::sext<n_words>(base_, base_, N - 1);
    }
  }

  // Copy a CANONICAL (already-masked, non-negative) narrower value's words into
  // this width. Unlike widen_ there is no sign fill to compute, and unlike
  // zext_to no mask to apply: the source's invariant guarantees both. Internal
  // (trailing underscore) -- this is Slop_u's mask-free widening path.
  template <int M>
  static constexpr Slop from_canonical_(const Slop<M>& s) {
    Slop          r;  // Integer, zeroed
    constexpr int cw = (Slop<M>::n_words < n_words) ? Slop<M>::n_words : n_words;
    for (int i = 0; i < cw; ++i) {
      r.base_[i] = s.base_[i];
    }
    return r;
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
  // bits = magnitude+1 on every computed output). Every non-width-reducing op
  // below also enforces at compile time that N is at least every operand's
  // carrier width. There is deliberately no runtime check.
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

  // Read a Slop<M>'s words AT THIS WIDTH. When M already carries this width's
  // word count, widen_ degenerates to copying the array onto the stack word for
  // word (the `Slop<M>::n_words < n_words` fill is compiled out), so hand back a
  // reference to the source words instead and let the op read them in place.
  // Only a genuine word-count change materializes a temporary.
  //
  // This is the common case for the ops that do NOT grow a value -- and/or/xor
  // -- where the graph hands both operands and the result the same width, and
  // it also covers same-word-count mismatches like Slop<96>::and_op(Slop<66>,
  // Slop<80>). The returned reference is to the caller's operand, which outlives
  // the full expression; the widen_ temporary lives to the end of it.
  template <int M>
  static constexpr decltype(auto) words_(const Slop<M>& s) {
    if constexpr (Slop<M>::n_words == n_words) {
      return (s.base_);  // const std::array<int64_t, n_words>& -- no copy
    } else {
      return widen_(s);  // by value
    }
  }

  // True when x, y and the result all fit a single word -- the scalar fast path.
  template <int A, int B>
  static constexpr bool one_word_ = (n_words == 1 && Slop<A>::n_words == 1 && Slop<B>::n_words == 1);

  // Widen a CANONICAL source: the invariant guarantees every bit at and above
  // the source width is zero, so the upper words are compile-time zero and the
  // runtime sign fill widen_ computes drops out entirely.
  template <int M>
  static constexpr std::array<int64_t, n_words> zero_widen_(const Slop<M>& s) {
    std::array<int64_t, n_words> a{};
    constexpr int                cw = (Slop<M>::n_words < n_words) ? Slop<M>::n_words : n_words;
    for (int i = 0; i < cw; ++i) {
      a[i] = s.base_[i];
    }
    return a;
  }

  template <Slop_operand T>
  static constexpr const Slop<Slop_arg<T>::bits>& sref_(const T& t) {
    return Slop_arg<T>::s(t);
  }

  template <Slop_operand T>
  static constexpr decltype(auto) awords_(const T& t) {
    constexpr int  M = Slop_arg<T>::bits;
    const Slop<M>& s = Slop_arg<T>::s(t);
    if constexpr (Slop<M>::n_words == n_words) {
      return (s.base_);
    } else if constexpr (Slop_arg<T>::canonical) {
      return zero_widen_(s);
    } else {
      return widen_(s);
    }
  }

  // one_word_ over operand TYPES rather than raw widths: a Slop_u<W> carries
  // its value in a Slop<W+1>, so the scalar fast path has to ask the trait.
  template <typename X, typename Y>
  static constexpr bool one_word_a_
      = (n_words == 1 && Slop<Slop_arg<X>::bits>::n_words == 1 && Slop<Slop_arg<Y>::bits>::n_words == 1);

  // Fixed-width Slop is only a materialization of an unlimited-precision HLOP
  // value. Codegen may choose a wider result carrier, but an ordinary operation
  // must never choose one narrower than an input carrier. Explicitly reducing
  // operations (comparison, remainder, AND masks/extracts, arithmetic right
  // shift) do not use this check.
  template <int... InputBits>
  static consteval void input_width_check() {
    static_assert(((N >= InputBits) && ...), "Slop result is narrower than an input; code generation would lose precision");
  }

  template <Slop_operand X, Slop_operand Y>
  static Slop add_op(const X& xa, const Y& ya) {
    const auto& x = sref_(xa);
    const auto& y = sref_(ya);
    input_width_check<Slop_arg<X>::bits, Slop_arg<Y>::bits>();
    Slop r;
    if constexpr (one_word_a_<X, Y>) {
      r.base_[0] = x.base_[0] + y.base_[0];
    } else {
      Blop::add<n_words>(r.base_, awords_(xa), awords_(ya));
    }
    return r;
  }

  template <Slop_operand X, Slop_operand Y>
  static Slop sub_op(const X& xa, const Y& ya) {
    const auto& x = sref_(xa);
    const auto& y = sref_(ya);
    input_width_check<Slop_arg<X>::bits, Slop_arg<Y>::bits>();
    Slop r;
    if constexpr (one_word_a_<X, Y>) {
      r.base_[0] = x.base_[0] - y.base_[0];
    } else {
      // One borrow chain, not a negate pass followed by a carry chain: the old
      // spelling walked the words twice and cost ~50% more instructions than
      // add_op at the same width.
      Blop::sub<n_words>(r.base_, awords_(xa), awords_(ya));
    }
    return r;
  }

  template <Slop_operand X, Slop_operand Y>
  static Slop mult_op(const X& xa, const Y& ya) {
    const auto& x = sref_(xa);
    const auto& y = sref_(ya);
    input_width_check<Slop_arg<X>::bits, Slop_arg<Y>::bits>();
    Slop r;
    if constexpr (one_word_a_<X, Y>) {
      r.base_[0] = x.base_[0] * y.base_[0];
    } else {
      Blop::mult<n_words>(r.base_, awords_(xa), awords_(ya));
    }
    return r;
  }

  template <Slop_operand X, Slop_operand Y>
  static Slop and_op(const X& xa, const Y& ya) {
    const auto& x = sref_(xa);
    const auto& y = sref_(ya);
    Slop        r;
    if constexpr (one_word_a_<X, Y>) {
      r.base_[0] = x.base_[0] & y.base_[0];
    } else {
      Blop::band<n_words>(r.base_, awords_(xa), awords_(ya));
    }
    return r;
  }

  template <Slop_operand X, Slop_operand Y>
  static Slop or_op(const X& xa, const Y& ya) {
    const auto& x = sref_(xa);
    const auto& y = sref_(ya);
    input_width_check<Slop_arg<X>::bits, Slop_arg<Y>::bits>();
    Slop r;
    if constexpr (one_word_a_<X, Y>) {
      r.base_[0] = x.base_[0] | y.base_[0];
    } else {
      Blop::bor<n_words>(r.base_, awords_(xa), awords_(ya));
    }
    return r;
  }

  template <Slop_operand X, Slop_operand Y>
  static Slop xor_op(const X& xa, const Y& ya) {
    const auto& x = sref_(xa);
    const auto& y = sref_(ya);
    input_width_check<Slop_arg<X>::bits, Slop_arg<Y>::bits>();
    Slop r;
    if constexpr (one_word_a_<X, Y>) {
      r.base_[0] = x.base_[0] ^ y.base_[0];
    } else {
      Blop::bxor<n_words>(r.base_, awords_(xa), awords_(ya));
    }
    return r;
  }

  template <Slop_operand X>
  static Slop not_op(const X& xa) {
    input_width_check<Slop_arg<X>::bits>();
    Slop r;
    if constexpr (n_words == 1 && Slop<Slop_arg<X>::bits>::n_words == 1) {
      r.base_[0] = ~sref_(xa).base_[0];
    } else {
      Blop::bnot<n_words>(r.base_, awords_(xa));
    }
    return r;
  }

  // Comparisons materialize a 0/1 MAGNITUDE at this width. The member forms
  // return create_bool(), whose true value is all-ones (-1) -- correct for a
  // Boolean-typed Slop, but it forced cgen to append `.zext_to<1>().zext_to<W>()`
  // to every compare (740 sites in one design) to recover the 0/1 an LGraph
  // LT/GT/EQ cell is defined to produce. These give cgen that value directly.
  template <Slop_operand X, Slop_operand Y>
  static Slop eq_op(const X& xa, const Y& ya) {
    const auto& x = sref_(xa);
    const auto& y = sref_(ya);
    Slop        r;
    if constexpr (one_word_a_<X, Y>) {
      r.base_[0] = (x.base_[0] == y.base_[0]) ? 1 : 0;
    } else {
      r.base_[0] = Blop::eq<n_words>(awords_(xa), awords_(ya)) ? 1 : 0;
    }
    return r;
  }

  template <Slop_operand X, Slop_operand Y>
  static Slop lt_op(const X& xa, const Y& ya) {
    const auto& x = sref_(xa);
    const auto& y = sref_(ya);
    Slop        r;
    if constexpr (one_word_a_<X, Y>) {
      r.base_[0] = (x.base_[0] < y.base_[0]) ? 1 : 0;
    } else {
      r.base_[0] = Blop::lt<n_words>(awords_(xa), awords_(ya)) ? 1 : 0;
    }
    return r;
  }

  template <Slop_operand X, Slop_operand Y>
  static Slop gt_op(const X& xa, const Y& ya) {
    const auto& x = sref_(xa);
    const auto& y = sref_(ya);
    Slop        r;
    if constexpr (one_word_a_<X, Y>) {
      r.base_[0] = (x.base_[0] > y.base_[0]) ? 1 : 0;
    } else {
      r.base_[0] = Blop::lt<n_words>(awords_(ya), awords_(xa)) ? 1 : 0;
    }
    return r;
  }

  // Shifts: the amount is a plain count, never a materialized Slop constant.
  // cgen previously built a full Slop<W>::create_integer(k) operand just to
  // pass a shift count (788 sites in one design, 270 of them multi-word).
  template <Slop_operand X>
  static Slop shl_op(const X& xa, int64_t amount) {
    input_width_check<Slop_arg<X>::bits>();
    Slop r;
    // No `amount == 0` early-out: Blop::shl already handles a zero count
    // (`x << 0` for one word, the plain word copy for many), so the test only
    // added a compare + select on every RUNTIME shift amount -- a constant
    // amount folds either way.
    Blop::shl<n_words>(r.base_, awords_(xa), amount);
    return r;
  }

  template <Slop_operand X, Slop_operand Amount>
  static Slop shl_op(const X& xa, const Amount& amount) {
    input_width_check<Slop_arg<X>::bits>();
    return shl_op(xa, sref_(amount).base_[0]);
  }

  template <Slop_operand X>
  static Slop sra_op(const X& xa, int64_t amount) {
    Slop r;
    Blop::shr<n_words>(r.base_, awords_(xa), amount);  // zero count handled inside
    return r;
  }

  template <Slop_operand X, Slop_operand Amount>
  static Slop sra_op(const X& xa, const Amount& amount) {
    return sra_op(xa, sref_(amount).base_[0]);
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
    Slop result;
    if constexpr (n_words == 1) {
      result.base_[0] = base_[0] - other.base_[0];
    } else {
      Blop::sub<n_words>(result.base_, base_, other.base_);  // one borrow chain, not neg + add
    }
    return result;
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
  // The `amount == 0` early-outs here are NOT a fast path (Blop::shl/shr both
  // handle a zero count, and a constant amount folds either way): they are what
  // keeps the TYPE TAG. `return *this` preserves a Boolean/String tag, while
  // falling through builds an Integer-tagged result -- and identical() compares
  // the tag, so dropping them would make a change-gated sim compare report a
  // spurious change. The static forms below have no such branch because they
  // never preserved the tag: both of their paths build a fresh Integer Slop.
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
      const uint64_t expect = range_word_mask_(w, lo, hi);
      if ((static_cast<uint64_t>(m.base_[w]) & expect) != expect) {
        return false;
      }
    }
    return true;
  }

  // The 64 bits of the contiguous mask [lo, hi) that live in word `w`. Only
  // words in [lo/64, (hi-1)/64] are asked for; every other word of the mask is
  // all-zero, so callers skip them instead of AND-ing with 0.
  static constexpr uint64_t range_word_mask_(int w, int lo, int hi) {
    uint64_t m = ~uint64_t(0);
    if (w == lo / 64) {
      m &= ~uint64_t(0) << (lo % 64);
    }
    if (w == (hi - 1) / 64 && (hi % 64) != 0) {
      m &= ~uint64_t(0) >> (64 - hi % 64);
    }
    return m;
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
    const auto     xword
        = [&](int idx) -> uint64_t { return (idx >= 0 && idx < Slop<A>::n_words) ? static_cast<uint64_t>(x.base_[idx]) : xsign; };
    const int nw = (len + 63) / 64;
    for (int w = 0; w < nw && w < n_words; ++w) {
      const int j  = lo + w * 64;
      const int wi = j / 64, sh = j % 64;
      uint64_t  v = sh == 0 ? xword(wi) : (xword(wi) >> sh) | (xword(wi + 1) << (64 - sh));
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
  //
  // The mask goes through Slop_arg, so a Slop_u<N-1> mask is accepted with no
  // conversion. A canonical mask is never negative, so it always takes the
  // contiguous fast path's `!is_negative()` arm.
  template <Slop_operand MT>
  Slop get_mask_op(const MT& mask_a) const {
    static_assert(Slop_arg<MT>::bits == N, "get_mask_op mask must be at this width (Slop<N> or Slop_u<N-1>)");
    const auto& mask = sref_(mask_a);
    nil_check_(mask);

    // FAST PATH — positive contiguous mask [lo, hi): the extract is a
    // word-wise shift (see set_mask_op's twin note; the per-bit walk below
    // dominated wide-datapath simulation). Keeps the member form's signed
    // single-bit quirk.
    //
    // Nothing here reads a bit count of the SOURCE, so with the constant mask
    // cgen emits for a Get_mask cell the whole call folds to a shift and an
    // AND at the call site. That is why the general gather lives in its own
    // function below: inlined here, its per-bit loops made this body too big
    // for clang to inline at all, and every Get_mask paid a real call.
    if (!mask.is_negative()) {
      int lo = 0, hi = 0;
      if (contiguous_range_(mask, lo, hi)) {
        if (hi - lo == 1) {
          return create_integer(bit_test(lo) ? -1 : 0);
        }
        return extract_bits_(*this, lo, hi - lo);
      }
    }

    return get_mask_gather_(mask);
  }

  // The general (non-contiguous or negative) mask gather: walk the selected
  // positions and pack them LSB-first. Deliberately out of line -- see above.
  [[gnu::noinline]] Slop get_mask_gather_(const Slop& mask) const {
    bool mask_neg           = mask.is_negative();
    int  mask_bits          = mask.get_bits();
    int  positive_mask_bits = mask_neg ? (mask_bits - 1) : mask_bits;
    int  src_bits           = get_bits();

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
  template <Slop_operand XT, Slop_operand MT>
  static Slop get_mask_op(const XT& xa, const MT& ma) {
    const auto& x    = sref_(xa);
    const auto& mask = sref_(ma);
    // FAST PATH — positive contiguous mask [lo, hi): a word-wise shift of x,
    // zero-filled above (the unsigned LSB-first pack this form is defined to
    // produce, single selected bit included). Kept small and free of any
    // source bit count so a constant mask folds to a shift + AND; the per-bit
    // walk lives out of line for the same reason as the member form's.
    if (!mask.is_negative()) {
      int lo = 0, hi = 0;
      if (Slop<Slop_arg<MT>::bits>::contiguous_range_(mask, lo, hi)) {
        return extract_bits_(x, lo, hi - lo);
      }
    }

    return get_mask_gather_(x, mask);
  }

  template <int A, int M>
  [[gnu::noinline]] static Slop get_mask_gather_(const Slop<A>& x, const Slop<M>& mask) {
    const bool mask_neg           = mask.is_negative();
    const int  mask_bits          = mask.get_bits();
    const int  positive_mask_bits = mask_neg ? (mask_bits - 1) : mask_bits;
    const int  src_bits           = x.get_bits();

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
  template <Slop_operand MT, Slop_operand VT>
  Slop set_mask_op(const MT& mask_a, const VT& value_a) const {
    static_assert(Slop_arg<MT>::bits == N && Slop_arg<VT>::bits == N,
                  "set_mask_op operands must be at this width (Slop<N> or Slop_u<N-1>)");
    const auto& mask  = sref_(mask_a);
    const auto& value = sref_(value_a);
    nil_check_(mask);
    I(!value.is_nil());
    if (mask.is_known_false()) {
      return *this;
    }

    // FAST PATH — a positive CONTIGUOUS mask [lo, hi): the shape every packed
    // field write lowers to. The generic loop below walks EVERY bit of the
    // result; on wide datapath Slops (the 500-1000+ bit VPU vectors of
    // lhdsuite's minion) that per-bit walk dominated whole-design simulation.
    // A contiguous range is a word-wise splice, which is exactly what
    // set_mask_op_opt does. Bit-for-bit identical to the loop (value bits map
    // LSB-first onto the selected range; bits at/above out_bits never write
    // and never consume value bits, hence the `hi` cap).
    //
    // The cap is `min(hi, N)`, not `min(hi, out_bits)`: for a positive mask
    // out_bits is min(N, max(src_bits, hi+1)), which is >= hi whenever hi <= N
    // and exactly N when hi > N -- so the two agree, and the fast path does
    // not have to compute the source/value/mask bit counts at all (three clz
    // sweeps that a constant mask cannot fold away, since src and value are
    // runtime values).
    if (!mask.is_negative()) {
      int lo = 0, hi = 0;
      if (contiguous_range_(mask, lo, hi)) {
        return set_mask_op_opt(lo, hi > N ? N : hi, value);
      }
    }

    return set_mask_scatter_(mask, value);
  }

  // The general (non-contiguous or negative) mask scatter. Out of line so the
  // fast path above stays inlinable -- same reasoning as get_mask_gather_.
  [[gnu::noinline]] Slop set_mask_scatter_(const Slop& mask, const Slop& value) const {
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

  // --- Contiguous-range mask writes (`_opt`) ---
  //
  // `_opt` marks an OPTIMIZED SPECIAL CASE, not an LGraph cell: unlike
  // set_mask_op these have no Set_mask counterpart in livehd graph/cell.* to
  // keep name-for-name in sync. They are set_mask_op narrowed to the mask shape
  // a packed-field write always has — one contiguous run of ones.
  //
  // set_mask_op already fast-paths that shape, but the CALLER still has to
  // materialize the mask, and the callee still has to rediscover its range on
  // every execution. On a wide datapath that is expensive for what it says:
  //
  //   v.set_mask_op(Slop<544>::from_pyrope("0x0..0ffff..ffff0000000000000000"),
  //                 Slop<544>::create_integer(0))
  //
  // pays for two nine-word constants, a get_bits(), a ctz/clz sweep over all
  // nine mask words and a nine-word contiguity check — to express "clear bits
  // 64..255", which the code generator knew literally. Spelled directly:
  //
  //   v.clear_mask_op_opt(64, 256)
  //
  // With literal bounds (what cgen emits) the whole word walk constant-folds:
  // only the words the range actually touches survive.
  //
  // THE RANGE IS HALF-OPEN [lo, hi) — bit `lo` is included, bit `hi` is not.
  // Same convention as contiguous_range_() above and Dlop::get_mask_range().
  //
  // Bit-exact with the general form for every 0 <= lo <= hi <= N:
  //   x.set_mask_op_opt(lo, hi, value) == x.set_mask_op(m, value)
  //   x.clear_mask_op_opt(lo, hi)      == x.set_mask_op(m, create_integer(0))
  // where `m` is the positive Slop<N> holding exactly bits [lo, hi). An empty
  // range (hi <= lo) returns *this, matching set_mask_op's zero-mask early-out.
  // The type_ tag and every bit outside [lo, hi) — including the sign-extension
  // region above get_bits() — carry through untouched, as in set_mask_op.

  // Replace bits [lo, hi) with the low (hi - lo) bits of `value`, value's LSB
  // landing at bit `lo`. `value` is read SIGNED: a range wider than the value's
  // significant bits is filled with its sign, exactly as set_mask_op's LSB-first
  // value.bit_test() walk does (bit_test sign-extends past storage).
  //
  // `value` is taken through Slop_arg, so a Slop_u<N-1> is accepted with NO
  // conversion: its carrier IS Slop<N>. The signed read above stays correct for
  // it -- a canonical value is never negative, so the sign fill is zeros, which
  // is exactly the unsigned read.
  template <Slop_operand V>
  Slop set_mask_op_opt(int lo, int hi, const V& value_a) const {
    static_assert(Slop_arg<V>::bits == N, "set_mask_op_opt value must be at this width (Slop<N> or Slop_u<N-1>)");
    const auto& value = sref_(value_a);
    nil_check_();
    I(!value.is_nil());
    I(lo >= 0 && hi <= N, "set_mask_op_opt range is outside Slop<N>");

    Slop result = *this;
    if (hi <= lo) {
      return result;
    }

    // ONE-WORD FAST PATH — the whole splice is a single masked merge. Not just
    // fewer instructions: the word-wise loop and its two lambdas below put the
    // function over clang's inlining budget, so every packed-field write at
    // N <= 64 (the dominant width) paid a real call. `lo < hi <= N <= 64` here,
    // so the shift is always in range.
    if constexpr (n_words == 1) {
      if (lo >= 64) {
        return result;  // outside this width entirely; the loop below would not run either
      }
      const uint64_t m = range_word_mask_(0, lo, hi);
      const uint64_t v = static_cast<uint64_t>(value.base_[0]) << lo;
      result.base_[0]  = static_cast<int64_t>((static_cast<uint64_t>(result.base_[0]) & ~m) | (v & m));
      return result;
    }

    const uint64_t vsign = value.is_negative() ? ~uint64_t(0) : uint64_t(0);
    const auto     vword
        = [&](int idx) -> uint64_t { return (idx >= 0 && idx < n_words) ? static_cast<uint64_t>(value.base_[idx]) : vsign; };
    const auto vshifted = [&](int w) -> uint64_t {  // 64 bits of (value << lo) at word w
      const int j = w * 64 - lo;                    // w >= lo/64, so j > -64: both shifts are in range
      if (j >= 0) {
        const int wi = j / 64, sh = j % 64;
        return sh == 0 ? vword(wi) : (vword(wi) >> sh) | (vword(wi + 1) << (64 - sh));
      }
      return vword(0) << (-j);
    };
    for (int w = lo / 64; w <= (hi - 1) / 64 && w < n_words; ++w) {
      const uint64_t m = range_word_mask_(w, lo, hi);
      result.base_[w]  = static_cast<int64_t>((static_cast<uint64_t>(result.base_[w]) & ~m) | (vshifted(w) & m));
    }
    return result;
  }

  // Clear bits [lo, hi) — set_mask_op_opt(lo, hi, create_integer(0)) without
  // building (or reading) the zero operand.
  Slop clear_mask_op_opt(int lo, int hi) const {
    nil_check_();
    I(lo >= 0 && hi <= N, "clear_mask_op_opt range is outside Slop<N>");

    Slop result = *this;
    if (hi <= lo) {
      return result;
    }
    for (int w = lo / 64; w <= (hi - 1) / 64 && w < n_words; ++w) {
      result.base_[w] = static_cast<int64_t>(static_cast<uint64_t>(result.base_[w]) & ~range_word_mask_(w, lo, hi));
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

  // n-ary concat_op — the LGraph Concat cell, MSB-FIRST (Verilog `{a, b, c}`,
  // same operand order as the binary form above):
  //
  //   Slop<9>::concat_op(a, b) == (a << 5) | b     // a: Slop<3>, b: Slop_u<5>
  //
  // Lane widths come from the operand TYPES, not from each value's significant
  // bits (the binary form's get_bits()) -- that distinction is the whole point
  // of the cell. Dropping a lane's leading zeros would shift every lane above
  // it, so the width has to be declared, and here the type declares it: a
  // Slop<W> lane contributes its W raw bits (sign slot included), a Slop_u<W>
  // lane its W bits.
  //
  // MASKING is therefore a compile-time decision, and there is no overflow
  // check to run: a Slop<W> lane masks (storage above bit W-1 is deliberately
  // non-canonical -- Slop<3>{-1} has all 64 bits set), a Slop_u<W> lane does
  // not (its invariant already guarantees them clean). Packing Slop_u lanes is
  // pure word stitching.
  //
  // The result carrier must be at least the sum of the lane widths; a narrower
  // one is a compile error, never a truncation. The assembled bits are then
  // read back as a SIGNED N-bit value -- sign-extended from the TOP lane's
  // MSB, so Slop<8>::concat_op(Slop<3>{-1}, Slop_u<5>{31}) is -1 and the same
  // lanes through Slop<10>::concat_op keep that sign in bits [9:8]. Use
  // Slop_u<8>::concat_op for the zero-extended 255.
  //
  // ONE SPELLING IS UNREACHABLE: Slop<N>::concat_op(x) with a single lane of
  // exactly Slop<N>. The binary member form above is a non-template and wins
  // overload resolution against this template, then fails as "call to
  // non-static member function without an object argument". That degenerate
  // concat is the identity, so spell it `Slop<N>{x}`; every other single-lane
  // form (a different Slop width, or any Slop_u) resolves here as expected.
  template <typename... Lanes>
  static Slop concat_op(const Lanes&... lanes) {
    static_assert(sizeof...(Lanes) >= 1, "concat_op needs at least one lane");
    constexpr int total = concat_bits_<Lanes...>();
    static_assert(total <= N, "Slop concat_op result is narrower than the sum of its lane widths");
    Slop r;  // Integer, zeroed
    concat_into_(r, lanes...);
    Blop::sext<n_words>(r.base_, r.base_, total - 1);
    return r;
  }

  // Sum of the lane widths. consteval so a bad width shows up as the
  // static_assert above rather than deep inside the fold.
  template <typename... Lanes>
  static consteval int concat_bits_() {
    return (Slop_lane<Lanes>::bits + ... + 0);
  }

  // Assemble lanes MSB-first into `r`, no landing conversion. Every lane is
  // masked into its own window, so bits at and above the total width stay zero
  // -- which is why Slop_u::concat_op can reuse this as-is (an unsigned result
  // has no sign to land) while Slop::concat_op sign-extends afterwards.
  static void concat_into_(Slop&) {}

  template <typename L0, typename... Rest>
  static void concat_into_(Slop& r, const L0& l0, const Rest&... rest) {
    // This lane sits above every lane still to its right.
    constexpr int off = (Slop_lane<Rest>::bits + ... + 0);
    r.template concat_lane_<Slop_lane<L0>::bits, off>(l0);
    concat_into_(r, rest...);
  }

  // OR one lane's W bits into [Off+W-1 : Off]. zext_to<W, N> is the one place
  // the per-lane mask can live, and it is elided entirely for a Slop_u lane.
  template <int W, int Off, typename L>
  void concat_lane_(const L& lane) {
    const Slop lw = lane.template zext_to<W, N>();
    if constexpr (Off == 0) {
      Blop::bor<n_words>(base_, base_, lw.base_);
    } else if constexpr (n_words == 1) {
      base_[0] |= static_cast<int64_t>(static_cast<uint64_t>(lw.base_[0]) << Off);
    } else {
      std::array<int64_t, n_words> shifted;
      Blop::shl<n_words>(shifted, lw.base_, Off);
      Blop::bor<n_words>(base_, base_, shifted);
    }
  }

  // --- Multiplexers / LUT (computing cells from livehd graph/cell.*) ---
  // Static, with the selector/address and ordered value list passed
  // explicitly. Slop carries no unknowns, so these are the plain concrete
  // cases of the matching Dlop ops.

  // mux_op: for two data arms the selector is a condition (zero selects arm 0,
  // any nonzero value selects arm 1), matching LNAST/LGraph Mux. With three or
  // more arms it is a zero-based index. A non-integer or out-of-range indexed
  // selector returns invalid(). Selector width is independent of data/result
  // width.
  template <int SelBits>
  static Slop mux_op(const Slop<SelBits>& sel, std::span<const Slop> values) {
    assert(!values.empty());
    if (values.size() == 2) {
      return sel.is_known_false() ? values[0] : values[1];
    }
    if (!sel.is_just_i64()) {
      return invalid();
    }
    int64_t idx = sel.to_just_i64();
    if (idx < 0 || static_cast<size_t>(idx) >= values.size()) {
      return invalid();
    }
    return values[idx];
  }
  template <int SelBits>
  static Slop mux_op(const Slop<SelBits>& sel, std::initializer_list<Slop> values) {
    return mux_op(sel, std::span<const Slop>(values.begin(), values.size()));
  }

  // Heterogeneous data-arm form used by generated LGraph kernels. Each arm is
  // losslessly promoted inside HLOP after the compile-time width contract.
  //
  // The promotion happens ONLY for the selected arm. Collecting the arms into
  // a std::array first (the obvious spelling) ran every arm's cross-width
  // sext and spilled the whole set to the stack to index it -- for the wide
  // muxes in a real design that is n-1 conversions and n*sizeof(Slop) bytes of
  // traffic thrown away. The short-circuiting `||` fold below stops at the
  // selected arm, so only its conversion is ever executed and nothing spills.
  template <Slop_operand Sel, Slop_operand... Vals>
  static Slop mux_op(const Sel& sel_a, const Vals&... values) {
    static_assert(sizeof...(Vals) > 0, "Mux requires at least one data input");
    input_width_check<Slop_arg<Vals>::bits...>();
    const auto&       sel = sref_(sel_a);
    constexpr int64_t n   = sizeof...(Vals);

    int64_t idx;
    if constexpr (n == 2) {
      idx = sel.is_known_false() ? 0 : 1;  // two arms: a condition, not an index
    } else {
      if (!sel.is_just_i64()) {
        return invalid();
      }
      idx = sel.to_just_i64();
      if (idx < 0 || idx >= n) {
        return invalid();
      }
    }
    return pick_arm_(idx, values...);
  }

  // Return the idx-th pack element promoted to this width, converting nothing
  // else. `idx` is already known to be in range.
  template <Slop_operand... Vals>
  static Slop pick_arm_(int64_t idx, const Vals&... values) {
    Slop    out;
    int64_t i = 0;
    (void)((idx == i++ ? (out = Slop{sref_(values)}, true) : false) || ...);
    return out;
  }

  // hotmux_op: one-hot selector — bit `i` selects values[i]. Selector width is
  // independent of the result/data width: a wide one-hot decode can select a
  // narrow value. The selector is asserted to be one-hot; an out-of-range hot
  // bit returns invalid().
  template <int SelBits>
  static Slop hotmux_op(const Slop<SelBits>& sel, std::span<const Slop> values) {
    assert(!values.empty());
    assert(sel.popcount() == 1 && "hotmux select must be one-hot");
    int b = sel.get_first_bit_set();
    if (b < 0 || static_cast<size_t>(b) >= values.size()) {
      return invalid();
    }
    return values[b];
  }
  template <int SelBits>
  static Slop hotmux_op(const Slop<SelBits>& sel, std::initializer_list<Slop> values) {
    return hotmux_op(sel, std::span<const Slop>(values.begin(), values.size()));
  }

  // Heterogeneous data-arm form. As with mux_op, promotion is lossless and an
  // undersized generated result is rejected while compiling the kernel.
  template <Slop_operand Sel, Slop_operand... Vals>
  static Slop hotmux_op(const Sel& sel_a, const Vals&... values) {
    static_assert(sizeof...(Vals) > 0, "Hotmux requires at least one data input");
    input_width_check<Slop_arg<Vals>::bits...>();
    const auto& sel = sref_(sel_a);
    assert(sel.popcount() == 1 && "hotmux select must be one-hot");
    const int b = sel.get_first_bit_set();
    if (b < 0 || b >= static_cast<int>(sizeof...(Vals))) {
      return invalid();
    }
    // Only the hot arm is promoted -- see mux_op's note.
    return pick_arm_(b, values...);
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
    auto        append_mag = [&](const Slop& mag) {
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
      const bool            neg = is_negative();
      const Slop            mag = neg ? neg_op() : *this;
      std::vector<uint64_t> limbs(mag.base_.begin(), mag.base_.end());
      constexpr uint64_t    kChunk = 10000000000000000000ULL;  // 10^19
      std::vector<uint64_t> chunks;
      auto                  nonzero = [&]() {
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
          unsigned __int128 cur         = (rem << 64) | limbs[static_cast<size_t>(i)];
          limbs[static_cast<size_t>(i)] = static_cast<uint64_t>(cur / kChunk);
          rem                           = cur % kChunk;
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
    int         nbits = get_bits();
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

// ── Slop_u<N>: canonical-unsigned materialization ────────────────────────────
//
// Slop<W> makes NO promise about raw storage above bit W-1: Slop<8>{-1} has all
// 64 bits set, and add_op at width 8 can leave 300 in the words. Masking is
// lazy — ops do not normalize, so every READ that needs exactness re-masks with
// zext_to. That is the right contract for single-use expression temporaries
// (they inline, and clang folds the masks), and the wrong one for values
// materialized ONCE and read MANY times: slots, named temps, state members,
// kernel ABI bindings. There each read pays the mask again.
//
// Slop_u<N> is the same value with the opposite contract:
//
//   raw storage is guaranteed to already be the result of zext_to<N>()
//
// i.e. Slop_u<N> ≅ Slop<N+1> (N magnitude bits + the always-zero sign slot)
// PLUS the promise that the zext has already happened. The mask lives in the
// constructor — ONE write-mask replacing N read-masks:
//
//   Slop_u<7> slot = expr;                 // one zext_to<7>, inside the ctor
//   acc = Slop<9>::add_op(slot.raw(), b);  // every read is raw — no zext_to
//
// Conversions: Slop_u<N> -> Slop<W> is free (already canonical, a word copy);
// Slop<M> -> Slop_u<N> costs the one materialization mask. Signed values are
// untouched — they keep lazy Slop<W> and the cross-width sext ctor.
//
// Arithmetic deliberately does NOT live here: the mixed-width statics
// (Slop<W>::add_op & co.) already accept operands at any width, so `.raw()`
// drops straight in and the result is a lazy Slop again. Only the ops that
// keep the value canonical for free — comparisons, and concat_op, whose lanes
// are masked into disjoint windows — return a Slop_u.
template <int N>
class Slop_u {
  static_assert(N >= 1, "Slop_u bit width must be >= 1");

  template <int>
  friend class Slop_u;
  // Slop::zext_to_u<W>() deposits an already-masked carrier.
  template <int>
  friend class Slop;

  static Slop_u from_canonical_(const Slop<N + 1>& masked) {
    Slop_u r;
    r.v_ = masked;
    return r;
  }

public:
  // N magnitude bits + the always-zero sign slot: the same word count as
  // Slop<N+1> (and as Slop<N> except when N is a multiple of 64).
  using Carrier              = Slop<N + 1>;
  static constexpr int width = N;

  // Invariant: v_ == v_.zext_to<N>() — Integer-tagged, every bit at and above
  // N zero, so the value is never negative and is always in [0, 2^N).
  constexpr Slop_u() = default;  // 0

  // The three materializing constructors. This is where the mask is paid, and
  // the only place: a Slop_u can only be built by masking.
  Slop_u(int64_t val) : v_(Carrier::create_integer(val).template zext_to<N, N + 1>()) {}

  // UNCHECKED and IMPLICIT, deliberately: this is the "mask whatever you were
  // given" entry point. It accepts a source of ANY width, so it cannot catch a
  // mis-sized codegen expression the way `Slop<W> v = expr;` does through the
  // EXPLICIT cross-width ctor. A caller that wants that check spells `land`
  // (below) instead.
  template <int M>
  Slop_u(const Slop<M>& s) : v_(s.template zext_to<N, N + 1>()) {}

  template <int M>
  Slop_u(const Slop_u<M>& s) : v_(s.template zext_to<N, N + 1>()) {}

  // The checked TEMP-DECLARATION landing for codegen. `Slop<W> v = expr;` got its
  // build-time width check from the EXPLICIT cross-width ctor; the Slop_u ctors
  // above are implicit and masking, so a mis-sized node_expr would silently
  // wrap instead of failing the build. `land` is the checked entry point:
  // node_expr MUST have landed at the declared carrier width N+1. A deliberate
  // narrowing (the sub_width_expr_ arm) is spelled `.zext_to_u<N>()` at the
  // call site, where it is visible.
  template <int M>
  static Slop_u land(const Slop<M>& s) {
    static_assert(M == N + 1, "codegen did not land the value at the declared carrier width N+1");
    return from_canonical_(s.template zext_to<N, N + 1>());
  }

  // A mixed Slop/Slop_u expression (most commonly a mux whose arms include an
  // already-canonical temporary) can already have the destination Slop_u type.
  // Keep the same checked-landing contract without forcing it back through its
  // Slop carrier: matching widths are an identity, mismatches remain a build
  // error instead of silently narrowing through a converting constructor.
  template <int M>
  static Slop_u land(const Slop_u<M>& s) {
    static_assert(M == N, "codegen did not land the value at the declared Slop_u width N");
    return s;
  }

  // Width-checked landing for a value whose producer has ALREADY proved the
  // canonical-unsigned invariant. Unlike land(), this does not mask: it is the
  // codegen fast path after bitwidth proves the result is in [0, 2^N). Keep it
  // separate from the converting constructors so ordinary callers cannot
  // accidentally turn an assumption into the default conversion behavior.
  template <int M>
  static Slop_u from_proven(const Slop<M>& s) {
    static_assert(M == N + 1, "proven value did not land at the declared carrier width N+1");
    return from_canonical_(s);
  }

  template <int M>
  static Slop_u from_proven(const Slop_u<M>& s) {
    static_assert(M == N, "proven value did not land at the declared Slop_u width N");
    return s;
  }

  static Slop_u create_integer(int64_t val) { return Slop_u(val); }
  // Literals land through the signed parser and are then masked to N bits, so
  // from_pyrope("-1") is 2^N-1. Non-integer literals (strings, nil) have no
  // unsigned materialization — they collapse to their bit pattern as Integer.
  static Slop_u from_pyrope(std::string_view txt) { return Slop_u(Carrier::from_pyrope(txt)); }
  static Slop_u from_binary(std::string_view txt, bool unsigned_result) {
    return Slop_u(Carrier::from_binary(txt, unsigned_result));
  }

  // The raw read: no mask, ever. Feed it to the mixed-width Slop statics.
  const Carrier& raw() const { return v_; }

  // Keep the low min(N, W) bits at carrier width C. When W >= N — the case
  // every read hits — the invariant already guarantees the result, so this is
  // a word copy with NO mask. Only a genuine truncation (W < N) masks.
  template <int W, int C = W>
  Slop<C> zext_to() const {
    static_assert(W >= 0, "zext_to width must be non-negative");
    static_assert(C >= W, "zext_to carrier is narrower than the zero-extended value");
    if constexpr (W >= N) {
      return Slop<C>::from_canonical_(v_);
    } else {
      return v_.template zext_to<W, C>();
    }
  }

  // Canonical -> canonical re-width. Widening (W >= N) is a word
  // copy; narrowing masks exactly once. This is the read every consumer of a
  // materialized unsigned temp wants, and the reason none of them needs a
  // separate .zext_to<>() first.
  template <int W>
  Slop_u<W> zext_to_u() const {
    if constexpr (W >= N) {
      return Slop_u<W>::from_canonical_(Slop<W + 1>::from_canonical_(v_));
    } else {
      return Slop_u<W>::from_canonical_(v_.template zext_to<W, W + 1>());
    }
  }

  // STATIC cell-shaped compares, mirroring Slop<W>::eq_op. Operands
  // at ANY width and ANY canonicality; the 0/1 result is canonical by
  // construction, so cgen's compare cells land in Slop_u with no conversion on
  // either side.
  //
  // The 0/1 is rebuilt with create_integer rather than landed through the
  // cross-width ctor: `Slop<cw>::eq_op` returns its true value as base_[0] == 1,
  // and at cw == 1 that bit IS the sign slot, so `Slop<N+1>{...}` would
  // sign-extend it to -1 and deposit a value that breaks the invariant.
  // is_known_true() reads the words, not the sign, so it is exact at every cw.
  template <Slop_operand X, Slop_operand Y>
  static Slop_u eq_op(const X& x, const Y& y) {
    static_assert(N >= 1, "compare result needs one magnitude bit");
    constexpr int cw = (Slop_arg<X>::bits > Slop_arg<Y>::bits ? Slop_arg<X>::bits : Slop_arg<Y>::bits);
    return from_canonical_(Carrier::create_integer(Slop<cw>::eq_op(x, y).is_known_true() ? 1 : 0));
  }
  template <Slop_operand X, Slop_operand Y>
  static Slop_u lt_op(const X& x, const Y& y) {
    static_assert(N >= 1, "compare result needs one magnitude bit");
    constexpr int cw = (Slop_arg<X>::bits > Slop_arg<Y>::bits ? Slop_arg<X>::bits : Slop_arg<Y>::bits);
    return from_canonical_(Carrier::create_integer(Slop<cw>::lt_op(x, y).is_known_true() ? 1 : 0));
  }
  template <Slop_operand X, Slop_operand Y>
  static Slop_u gt_op(const X& x, const Y& y) {
    static_assert(N >= 1, "compare result needs one magnitude bit");
    constexpr int cw = (Slop_arg<X>::bits > Slop_arg<Y>::bits ? Slop_arg<X>::bits : Slop_arg<Y>::bits);
    return from_canonical_(Carrier::create_integer(Slop<cw>::gt_op(x, y).is_known_true() ? 1 : 0));
  }

  // n-ary concat_op — MSB-first, same lane rules as Slop::concat_op (see the
  // comment there). The unsigned landing: each lane is masked into its own
  // window, so the assembly is already canonical and there is nothing to do
  // afterwards. With Slop_u lanes this is pure word stitching — no mask
  // anywhere on the pack path.
  template <typename... Lanes>
  static Slop_u concat_op(const Lanes&... lanes) {
    static_assert(sizeof...(Lanes) >= 1, "concat_op needs at least one lane");
    constexpr int total = Carrier::template concat_bits_<Lanes...>();
    static_assert(total <= N, "Slop_u concat_op result is narrower than the sum of its lane widths");
    Slop_u r;
    Carrier::concat_into_(r.v_, lanes...);
    return r;
  }

  // The ops that keep the value canonical FOR FREE, so the result is
  // named Slop_u with NO mask at all. Each precondition is a static_assert;
  // when it does not hold the call does not compile and codegen falls back to
  // the lazy Slop form plus one ctor mask.
  template <Slop_operand X, Slop_operand Y>
  static Slop_u and_op(const X& x, const Y& y) {
    static_assert(Slop_arg<X>::canonical || Slop_arg<Y>::canonical, "and_op needs at least ONE canonical operand");
    static_assert(Slop_arg<X>::bits <= N + 1 && Slop_arg<Y>::bits <= N + 1, "and_op operand wider than the canonical result");
    return from_canonical_(Carrier::and_op(x, y));
  }
  template <Slop_operand X, Slop_operand Y>
  static Slop_u or_op(const X& x, const Y& y) {
    static_assert(Slop_arg<X>::canonical && Slop_arg<Y>::canonical,
                  "or_op keeps canonicality only when BOTH operands are canonical");
    static_assert(Slop_arg<X>::bits <= N + 1 && Slop_arg<Y>::bits <= N + 1, "or_op operand wider than the canonical result");
    return from_canonical_(Carrier::or_op(x, y));
  }
  template <Slop_operand X, Slop_operand Y>
  static Slop_u xor_op(const X& x, const Y& y) {
    static_assert(Slop_arg<X>::canonical && Slop_arg<Y>::canonical,
                  "xor_op keeps canonicality only when BOTH operands are canonical");
    static_assert(Slop_arg<X>::bits <= N + 1 && Slop_arg<Y>::bits <= N + 1, "xor_op operand wider than the canonical result");
    return from_canonical_(Carrier::xor_op(x, y));
  }
  // `amount` is a RUNTIME count, so the bound must hold at amount == 0 too: a
  // right shift keeps at most the operand's M magnitude bits, and the result is
  // canonical at N only when N >= M. (M <= N+1 would admit M == N+1, which
  // deposits a value with bit N set — an invariant break, not a truncation.)
  template <int M>
  static Slop_u sra_op(const Slop_u<M>& x, int64_t amount) {
    static_assert(M <= N, "sra_op result is narrower than the operand");
    return from_canonical_(Carrier::sra_op(x, amount));
  }
  template <Slop_operand X, Slop_operand Y>
  static Slop_u add_op(const X& x, const Y& y) {
    static_assert(Slop_arg<X>::canonical && Slop_arg<Y>::canonical, "add_op keeps canonicality only for canonical operands");
    constexpr int wider = (Slop_arg<X>::bits > Slop_arg<Y>::bits) ? Slop_arg<X>::bits : Slop_arg<Y>::bits;
    static_assert(N + 1 >= wider + 1, "add_op needs one carry bit of headroom to stay canonical");
    return from_canonical_(Carrier::add_op(x, y));
  }

  // sub_op is the one op whose canonicality CANNOT be decided from the operand
  // widths: `x - y` is non-negative exactly when x >= y, a VALUE fact. LiveHD's
  // bitwidth pass proves that range in some cones and stamps the result
  // unsigned; this entry point is for those, and the proof is the CALLER's
  // obligation.
  //
  // It ASSERTS the obligation and MASKS anyway, and it needs to do both:
  //
  //  * the assert is what makes an over-claimed stamp findable. A borrow-out
  //    fills the top word, so `is_negative()` on the carrier is an exact test
  //    for the violation -- one word compare, compiled out in release. This is
  //    the only check in this class that can catch a bad `unsign` stamp at all,
  //    because the failure shows up in the computed VALUE, not in a width.
  //
  //  * the mask is what keeps a violation from escaping. Depositing the
  //    negative carrier would hand downstream code a Slop_u that breaks the
  //    invariant every other entry point here relies on (or_op assumes both
  //    operands canonical, concat_op assumes lanes pre-masked, the comparisons
  //    assume non-negative) -- one bad value would corrupt arbitrarily far.
  //    Masking instead yields the mod-2^N wrap, which is BOTH what the lazy
  //    Slop form already produces (its consumers each re-mask) and what the
  //    hardware does. So release behaviour is unchanged and the invariant
  //    holds unconditionally.
  //
  // Cost is therefore one mask, same as not_op/shl_op below -- and it is the
  // same trade: one mask at the write replaces one per read.
  template <Slop_operand X, Slop_operand Y>
  static Slop_u sub_op(const X& x, const Y& y) {
    static_assert(Slop_arg<X>::canonical && Slop_arg<Y>::canonical, "sub_op keeps canonicality only for canonical operands");
    static_assert(N + 1 >= Slop_arg<X>::bits && N + 1 >= Slop_arg<Y>::bits, "sub_op operand wider than the canonical result");
    const Carrier r = Carrier::sub_op(x, y);
    I(!r.is_negative(), "Slop_u sub_op went negative: the unsigned stamp on this cell does not hold");
    return from_canonical_(r.template zext_to<N, N + 1>());
  }

  // Product of two non-negative values is non-negative and needs the sum of
  // their magnitudes. No mask.
  template <Slop_operand X, Slop_operand Y>
  static Slop_u mult_op(const X& x, const Y& y) {
    static_assert(Slop_arg<X>::canonical && Slop_arg<Y>::canonical, "mult_op keeps canonicality only for canonical operands");
    static_assert(N >= (Slop_arg<X>::bits - 1) + (Slop_arg<Y>::bits - 1),
                  "mult_op result needs the SUM of the operand magnitudes to stay canonical");
    return from_canonical_(Carrier::mult_op(x, y));
  }

  // A SELECT copies one arm, so the result is canonical exactly when every arm
  // is. No mask on any path.
  //
  // DIVERGENCE from Slop::mux_op / Slop::hotmux_op, which return invalid() on
  // an out-of-range (or non-one-hot) selector: Slop_u has no type tag, so it
  // has no invalid() to return. The bad selector is a codegen error, so it is
  // an assert here and yields 0 in a release build. A caller that needs the
  // invalid() tag must stay on the Slop form.
  template <Slop_operand Sel, Slop_operand... Vals>
  static Slop_u mux_op(const Sel& sel, const Vals&... values) {
    static_assert(sizeof...(Vals) > 0, "Mux requires at least one data input");
    static_assert((Slop_arg<Vals>::canonical && ...), "mux_op keeps canonicality only when EVERY arm is canonical");
    static_assert(((Slop_arg<Vals>::bits <= N + 1) && ...), "mux_op arm wider than the canonical result");
    return from_canonical_(Carrier::mux_op(sel, values...));
  }

  template <Slop_operand Sel, Slop_operand... Vals>
  static Slop_u hotmux_op(const Sel& sel, const Vals&... values) {
    static_assert(sizeof...(Vals) > 0, "Hotmux requires at least one data input");
    static_assert((Slop_arg<Vals>::canonical && ...), "hotmux_op keeps canonicality only when EVERY arm is canonical");
    static_assert(((Slop_arg<Vals>::bits <= N + 1) && ...), "hotmux_op arm wider than the canonical result");
    return from_canonical_(Carrier::hotmux_op(sel, values...));
  }

  // A field splice into [lo, hi) leaves every bit at and above `hi` alone, so
  // with a canonical base and hi <= N the result is canonical. No mask. `lo`
  // and `hi` are runtime parameters (codegen passes literals), so the bound is
  // an assert rather than a static_assert.
  template <Slop_operand B, Slop_operand V>
  static Slop_u set_mask_op_opt(const B& base, int lo, int hi, const V& value) {
    static_assert(Slop_arg<B>::canonical, "set_mask_op_opt needs a canonical base to stay canonical");
    static_assert(Slop_arg<B>::bits == N + 1, "set_mask_op_opt base must be at the result width");
    static_assert(Slop_arg<V>::bits == N + 1, "set_mask_op_opt value must be at the result width");
    I(hi <= N, "set_mask_op_opt would write the canonical sign slot");
    return from_canonical_(Slop_arg<B>::s(base).set_mask_op_opt(lo, hi, value));
  }

  template <Slop_operand B>
  static Slop_u clear_mask_op_opt(const B& base, int lo, int hi) {
    static_assert(Slop_arg<B>::canonical, "clear_mask_op_opt needs a canonical base to stay canonical");
    static_assert(Slop_arg<B>::bits == N + 1, "clear_mask_op_opt base must be at the result width");
    I(hi <= N, "clear_mask_op_opt would write the canonical sign slot");
    return from_canonical_(Slop_arg<B>::s(base).clear_mask_op_opt(lo, hi));
  }

  // Member spellings used when a generated expression was written before its
  // base became canonical. They keep the source in Slop_u instead of forcing a
  // `.raw()` conversion at every call site. The contiguous forms preserve the
  // invariant and return Slop_u; the general runtime-mask form returns the lazy
  // carrier for the caller's checked landing.
  template <Slop_operand V>
  Slop_u set_mask_op_opt(int lo, int hi, const V& value) const {
    return Slop_u::set_mask_op_opt(*this, lo, hi, value);
  }

  Slop_u clear_mask_op_opt(int lo, int hi) const { return Slop_u::clear_mask_op_opt(*this, lo, hi); }

  template <Slop_operand M, Slop_operand V>
  Carrier set_mask_op(const M& mask, const V& value) const {
    return v_.set_mask_op(mask, value);
  }

  template <Slop_operand M>
  Carrier get_mask_op(const M& mask) const {
    return v_.get_mask_op(mask);
  }

  // The three below each cost ONE mask, unlike everything above. They are here
  // anyway because the mask REPLACES a conversion the caller was already
  // emitting -- codegen writes `Slop<W>::not_op(...)` and then a `.zext_to<>()`
  // to recover the unsigned value -- so naming the result Slop_u is a wash at
  // runtime and one fewer operation in the generated text.

  // ~x sets every bit above the operand's magnitude, so this one genuinely has
  // to mask. Nothing about the operand needs to be canonical.
  template <Slop_operand X>
  static Slop_u not_op(const X& x) {
    return from_canonical_(Carrier::not_op(x).template zext_to<N, N + 1>());
  }

  // The shift amount is a RUNTIME value, so the result width cannot be bounded
  // at compile time: mask.
  template <Slop_operand X>
  static Slop_u shl_op(const X& x, int64_t amount) {
    return from_canonical_(Carrier::shl_op(x, amount).template zext_to<N, N + 1>());
  }
  template <Slop_operand X, Slop_operand Amount>
  static Slop_u shl_op(const X& x, const Amount& amount) {
    return from_canonical_(Carrier::shl_op(x, amount).template zext_to<N, N + 1>());
  }

  // The STATIC Slop::get_mask_op already produces the unsigned LSB-first pack
  // (unlike the member form, whose single-bit result is the signed -1), so this
  // is the natural unsigned cell. The mask is a runtime operand, so the pack
  // width is not known at compile time: mask.
  template <Slop_operand X, Slop_operand M>
  static Slop_u get_mask_op(const X& x, const M& mask) {
    return from_canonical_(Carrier::get_mask_op(x, mask).template zext_to<N, N + 1>());
  }

  // --- Comparisons ---
  // Both sides are canonical and non-negative, so the carrier's signed compare
  // IS the unsigned compare. Deliberately Slop_u-only: comparing against a
  // lazily-masked Slop would silently mix signed and unsigned readings — spell
  // that one out through .raw() and the Slop statics.
  //
  // The compare runs at a width that covers BOTH carriers. The mixed-width
  // Slop statics read their operands AT THE RESULT WIDTH (words_()), so naming
  // a narrow result type here would truncate a wide operand to its low word --
  // Slop<2>::eq_op(Slop_u<100>, Slop_u<100>) compared 64 of the 100 bits.
  // max(N,M)+1 is exactly the wider carrier's word count, and every bit at and
  // above max(N,M) is zero by the invariant, so the signed compare on the top
  // word is still the unsigned compare.
  template <int M>
  using Cmp_ = Slop<((N > M) ? N : M) + 1>;

  template <int M>
  bool operator==(const Slop_u<M>& o) const {
    return Cmp_<M>::eq_op(v_, o.raw()).is_known_true();
  }
  template <int M>
  std::strong_ordering operator<=>(const Slop_u<M>& o) const {
    if (Cmp_<M>::lt_op(v_, o.raw()).is_known_true()) {
      return std::strong_ordering::less;
    }
    // Equality, not a second ordered compare: `eq` is a word-wise mismatch scan
    // that stops at the first differing word, where `gt` walks the operands the
    // way `lt` just did. Matters at the widths this header targets.
    return Cmp_<M>::eq_op(v_, o.raw()).is_known_true() ? std::strong_ordering::equal : std::strong_ordering::greater;
  }
  // Exact int64_t comparison — never masks the right-hand side, so a value
  // that cannot fit N bits simply compares false.
  bool operator==(int64_t v) const { return v >= 0 && v_.is_just_i64() && v_.to_just_i64() == v; }

  // Cell-shaped forms: 0/1 results, canonical by construction (prop §2.2's
  // "the ctor's zext is elided" population).
  template <int M>
  Slop_u<1> eq_op(const Slop_u<M>& o) const {
    return Slop_u<1>(static_cast<int64_t>(*this == o));
  }
  template <int M>
  Slop_u<1> lt_op(const Slop_u<M>& o) const {
    return Slop_u<1>(static_cast<int64_t>(*this < o));
  }
  template <int M>
  Slop_u<1> le_op(const Slop_u<M>& o) const {
    return Slop_u<1>(static_cast<int64_t>(*this <= o));
  }
  template <int M>
  Slop_u<1> gt_op(const Slop_u<M>& o) const {
    return Slop_u<1>(static_cast<int64_t>(*this > o));
  }
  template <int M>
  Slop_u<1> ge_op(const Slop_u<M>& o) const {
    return Slop_u<1>(static_cast<int64_t>(*this >= o));
  }

  // Representational equality, the sim's change-gated compare (same width, so
  // the invariant makes it exact).
  bool identical(const Slop_u& o) const { return v_.identical(o.v_); }

  // --- Queries (delegated; the value is always a non-negative Integer) ---
  // Two different widths are on offer here, deliberately:
  //   width      -- N, the DECLARED width. What a field/port/VCD signal is.
  //   get_bits() -- the Slop convention, magnitude + 1 sign slot, so a full
  //                 Slop_u<N> reports N+1. What a materialization needs.
  // to_binary() prints `width` bits; anything sizing a field must use `width`
  // (or to_binary().size()), never get_bits().
  bool              bit_test(int pos) const { return v_.bit_test(pos); }
  constexpr int     get_bits() const { return v_.get_bits(); }
  int               popcount() const { return v_.popcount(); }
  int               get_first_bit_set() const { return v_.get_first_bit_set(); }
  int               get_last_bit_set() const { return v_.get_last_bit_set(); }
  bool              is_known_false() const { return v_.is_known_false(); }
  bool              is_known_true() const { return v_.is_known_true(); }
  constexpr bool    is_negative() const { return false; }
  constexpr bool    has_unknowns() const { return false; }
  constexpr bool    is_just_i64() const { return v_.is_just_i64(); }
  constexpr int64_t to_just_i64() const { return v_.to_just_i64(); }
  constexpr int64_t to_i64_low() const { return v_.to_i64_low(); }

  // --- Conversion (checkpoint regs.json, VCD, pretty-printing) ---
  std::string to_string() const { return v_.to_string(); }
  // Exactly N bits — the DECLARED width, not Slop::to_binary()'s significant
  // bits (which would print the always-zero sign slot as a leading 0). A
  // canonical unsigned value has an exact width, so it prints at that width.
  std::string to_binary() const {
    std::string result;
    result.reserve(N);
    for (int i = N - 1; i >= 0; --i) {
      result.push_back(v_.bit_test(i) ? '1' : '0');
    }
    return result;
  }
  std::string to_binary(int digits, bool sep) const { return v_.to_binary(digits, sep); }
  std::string to_hex(int digits = 0, bool sep = false, bool upper = false) const { return v_.to_hex(digits, sep, upper); }
  std::string to_decimal(int digits = 0, bool sep = false) const { return v_.to_decimal(digits, sep); }
  std::string to_pyrope() const { return v_.to_pyrope(); }
  std::string to_verilog() const { return v_.to_verilog(); }

  void dump() const {
    std::print("Slop_u<{}> -> ", N);
    v_.dump();
  }

private:
  Carrier v_;
};

// Lane widths for concat_op, keyed by the operand type (see Slop::concat_op).
template <int W>
struct Slop_lane<Slop<W>> {
  static constexpr int bits = W;
};

template <int W>
struct Slop_lane<Slop_u<W>> {
  static constexpr int bits = W;
};

template <int W>
struct Slop_arg<Slop<W>> {
  static constexpr int  bits      = W;
  static constexpr bool canonical = false;
  static const Slop<W>& s(const Slop<W>& v) { return v; }
};

template <int W>
struct Slop_arg<Slop_u<W>> {
  static constexpr int      bits      = W + 1;
  static constexpr bool     canonical = true;
  static const Slop<W + 1>& s(const Slop_u<W>& v) { return v.raw(); }
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
Slop<B* static_cast<int>(S)> slop_read_all(const std::array<Slop<B>, S>& a) {
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
template <int DstBits, int SrcBits>
inline bool slop_update(Slop<DstBits>& dst, const Slop<SrcBits>& v) {
  static_assert(DstBits >= SrcBits, "Slop update destination is narrower than the source; code generation would lose precision");
  const Slop<DstBits> widened{v};
  if (dst.identical(widened)) {
    return false;
  }
  dst = widened;
  return true;
}
inline bool slop_update(bool& dst, bool v) {
  if (dst == v) {
    return false;
  }
  dst = v;
  return true;
}

// Same compare-on-write for a canonical-unsigned destination (unsigned state
// members / slots). No DstBits >= SrcBits check: writing a signed Slop<W> into
// a Slop_u<W-1> IS the intended materialization — the mask is the point, not a
// loss of precision — so the width relation is the emitter's call.
// The other direction -- writing a CANONICAL value into a lazily
// typed destination (a boundary slot, a state member, an output field). Free:
// the source's invariant means there is nothing to mask or sign-fill.
template <int DstBits, int SrcBits>
inline bool slop_update(Slop<DstBits>& dst, const Slop_u<SrcBits>& v) {
  static_assert(DstBits > SrcBits, "Slop{Slop_u} re-canonicalizes the sign when DstBits <= SrcBits");
  const Slop<DstBits> nv{v};
  if (dst.identical(nv)) {
    return false;
  }
  dst = nv;
  return true;
}

template <int DstBits, int SrcBits>
inline bool slop_update(Slop_u<DstBits>& dst, const Slop<SrcBits>& v) {
  const Slop_u<DstBits> masked{v};  // the one materialization mask
  if (dst.identical(masked)) {
    return false;
  }
  dst = masked;
  return true;
}

template <int DstBits, int SrcBits>
inline bool slop_update(Slop_u<DstBits>& dst, const Slop_u<SrcBits>& v) {
  static_assert(DstBits >= SrcBits, "Slop_u update destination is narrower than the source; code generation would lose precision");
  const Slop_u<DstBits> widened{v};
  if (dst.identical(widened)) {
    return false;
  }
  dst = widened;
  return true;
}
