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

#include <array>
#include <cassert>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <print>
#include <random>
#include <span>
#include <string>
#include <string_view>

#include "blop.hpp"
#include "iassert.hpp"

template <int N>
class Slop {
  static_assert(N >= 1, "Slop bit width must be >= 1");

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
  // has no symbolic unknowns, so this returns an N-bit random concrete value
  // drawn from the same PRNG used at parse time. In practice this is never
  // called for Slop in the shared kernels — the `has_unknowns()` guards always
  // take the known path — but the symbol must exist to compile.
  static Slop unknown(int nbits) {
    Slop s;
    if (nbits <= 0) {
      return s;
    }
    int words = std::min((nbits + 63) / 64, n_words);
    for (int i = 0; i < words; ++i) {
      static thread_local std::mt19937_64 rng{0xC0FFEEULL};
      s.base_[i] = static_cast<int64_t>(rng());
    }
    int leftover = nbits % 64;
    if (leftover > 0 && words - 1 < n_words) {
      int64_t mask        = (int64_t(1) << leftover) - 1;
      s.base_[words - 1] &= mask;
    }
    return s;
  }

  // Draw a single random bit from a deterministic per-process PRNG. Runtime
  // only — constant-evaluated calls hit the constexpr branch below and throw
  // (a '?' in a compile-time literal is not meaningful since the result would
  // be non-deterministic). Slop has no notion of unknowns at runtime, so we
  // randomize each '?'/'x'/'z' bit at parse time and store 0/1 in base_.
  static int random_bit_() {
    static thread_local std::mt19937_64 rng{0xC0FFEEULL};
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
          ++skip_chars;
          sel_ch = lower(orig_txt[skip_chars]);
          if (sel_ch != 'b') {
            throw std::runtime_error("ERROR: unknown pyrope encoding (only 0sb...)");
          }
        } else {
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

  // mod_op: integer remainder. Asserts on mod-by-zero. Single-word only (the
  // common case in Slop's runtime kernel); multi-word callers should fall
  // back to Dlop.
  Slop mod_op(const Slop& other) const {
    nil_check_(other);
    I(!other.is_known_false(), "Slop modulo by zero");
    return create_integer(base_[0] % other.base_[0]);
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

  Slop get_mask_op() const {
    nil_check_();
    if (!is_negative()) {
      return *this;
    }
    Slop result;
    int  nbits   = get_bits();
    result.base_ = base_;
    int top_word = nbits / 64;
    int top_bit  = nbits % 64;
    if (top_bit > 0 && top_word < n_words) {
      result.base_[top_word] &= (int64_t(1) << top_bit) - 1;
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
    if (!sel.is_i()) {
      return invalid();
    }
    int64_t idx = sel.to_i();
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
    if (!addr.is_i()) {
      return invalid();
    }
    int64_t idx = addr.to_i();
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
    if (top_word < n_words && top_bit > 0) {
      result.base_[top_word] &= (int64_t(1) << top_bit) - 1;
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

  bool is_mask() const {
    if (is_negative()) {
      return false;
    }
    if constexpr (n_words == 1) {
      return base_[0] > 0 && ((base_[0] + 1) & base_[0]) == 0;
    } else {
      int top = n_words - 1;
      while (top > 0 && base_[top] == 0) {
        --top;
      }
      if (base_[top] <= 0) {
        return false;
      }
      if (((base_[top] + 1) & base_[top]) != 0) {
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
      return ((base_[nonzero_idx] - 1) & base_[nonzero_idx]) == 0;
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

  constexpr bool is_i() const { return get_bits() <= 62; }

  constexpr int64_t to_i() const {
    assert(is_i());
    return base_[0];
  }

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

  std::string to_binary() const {
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

    if (is_i()) {
      int64_t val = to_i();
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
    if (is_i()) {
      return std::format("{}'sh{:x}", nbits, static_cast<uint64_t>(base_[0]));
    }
    std::string hex;
    for (int i = n_words - 1; i >= 0; --i) {
      if (i == n_words - 1) {
        hex += std::format("{:x}", static_cast<uint64_t>(base_[i]));
      } else {
        hex += std::format("{:016x}", static_cast<uint64_t>(base_[i]));
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
