//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// slop: Static Logic Operation
//
// Template class where bit-width N is known at compile time.
// Stack-allocated value type — no dynamic memory allocation.
// Uses Blop primitives for all operations.
// Semantics match Dlop exactly (signed, unlimited-precision feel, unknowns via base/extra).

#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <format>
#include <print>
#include <string>
#include <string_view>

#include "blop.hpp"

template <int N>
class Slop {
  static_assert(N >= 1, "Slop bit width must be >= 1");

  static constexpr int n_words = (N + 63) / 64;

  enum class Type : int16_t {
    Invalid  = -1,
    Integer  = 0,
    Boolean  = 1,
    String   = 2,
    Bitwidth = 3
  };

  Type                          type_;
  std::array<int64_t, n_words>  base_;
  std::array<int64_t, n_words>  extra_;  // unknown mask (1 = unknown)

  constexpr Slop(Type tp, std::array<int64_t, n_words> b, std::array<int64_t, n_words> e)
      : type_(tp), base_(b), extra_(e) {}

  static constexpr std::array<int64_t, n_words> zero_array() {
    std::array<int64_t, n_words> a{};
    for (int i = 0; i < n_words; ++i) a[i] = 0;
    return a;
  }

  static constexpr std::array<int64_t, n_words> fill_array(int64_t v) {
    std::array<int64_t, n_words> a{};
    a[0] = v;
    int64_t fill = v < 0 ? -1 : 0;
    for (int i = 1; i < n_words; ++i) a[i] = fill;
    return a;
  }

  constexpr bool has_extra() const {
    for (int i = 0; i < n_words; ++i) {
      if (extra_[i] != 0) return true;
    }
    return false;
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
  constexpr Slop() : type_(Type::Integer), base_(zero_array()), extra_(zero_array()) {}

  constexpr Slop(int64_t val) : type_(Type::Integer), base_(fill_array(val)), extra_(zero_array()) {}

  // --- Factory methods ---
  static constexpr Slop create_bool(bool val) {
    return Slop(Type::Boolean, fill_array(val ? -1 : 0), zero_array());
  }

  static constexpr Slop create_integer(int64_t val) {
    return Slop(Type::Integer, fill_array(val), zero_array());
  }

  static constexpr Slop create_string(std::string_view txt) {
    Slop s(Type::String, zero_array(), zero_array());
    for (int i = txt.size() - 1; i >= 0; --i) {
      Blop::shl<n_words>(s.base_, s.base_, 8);
      s.base_[0] |= static_cast<unsigned char>(txt[i]);
    }
    return s;
  }

  static constexpr Slop from_string(std::string_view txt) {
    return create_string(txt);
  }

  static constexpr Slop invalid() {
    return Slop(Type::Invalid, zero_array(), zero_array());
  }

  // ref shares the Invalid tag with `invalid()` — distinguished by carrying
  // a non-zero byte-packed payload. Mirrors Lconst::from_ref encoding.
  static constexpr Slop from_ref(std::string_view txt) {
    Slop s(Type::Invalid, zero_array(), zero_array());
    for (int i = txt.size() - 1; i >= 0; --i) {
      Blop::shl<n_words>(s.base_, s.base_, 8);
      s.base_[0] |= static_cast<unsigned char>(txt[i]);
    }
    return s;
  }

  static Slop unknown(int nbits) {
    Slop s;
    if (nbits <= 0) return s;
    if (nbits <= 63) {
      int64_t mask = (int64_t(1) << nbits) - 1;
      s.base_[0]  = mask;
      s.extra_[0] = mask;
    } else {
      int words = (nbits + 63) / 64;
      for (int i = 0; i < std::min(words, n_words); ++i) {
        s.base_[i]  = -1;
        s.extra_[i] = -1;
      }
      int leftover = nbits % 64;
      if (leftover > 0 && words - 1 < n_words) {
        int64_t mask = (int64_t(1) << leftover) - 1;
        s.base_[words - 1]  = mask;
        s.extra_[words - 1] = mask;
      }
    }
    return s;
  }

  static Slop unknown_positive(int nbits) {
    if (nbits <= 1) return create_integer(0);
    return unknown(nbits - 1);
  }

  static Slop unknown_negative(int nbits) {
    if (nbits <= 1) return create_integer(-1);
    auto s = unknown(nbits);
    int word = (nbits - 1) / 64;
    int bit  = (nbits - 1) % 64;
    if (word < n_words) {
      s.extra_[word] &= ~(int64_t(1) << bit);
    }
    return s;
  }

  static constexpr Slop from_binary(std::string_view txt, bool unsigned_result) {
    Slop s;
    if (!unsigned_result) {
      for (size_t i = 0; i < txt.size(); ++i) {
        auto ch = txt[i];
        if (ch == '_') continue;
        if (ch == '1') {
          for (int w = 0; w < n_words; ++w) s.base_[w] = -1;
        } else if (ch == '?') {
          for (int w = 0; w < n_words; ++w) s.extra_[w] = -1;
        }
        break;
      }
    }

    for (size_t i = 0; i < txt.size(); ++i) {
      auto ch = txt[i];
      if (ch == '_') continue;

      Blop::shl<n_words>(s.base_, s.base_, 1);
      Blop::shl<n_words>(s.extra_, s.extra_, 1);
      if (ch == '?' || ch == 'x' || ch == 'z') {
        s.extra_[0] |= 1;
        s.base_[0]  |= 1;
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
    if (orig_txt.empty()) return invalid();

    // Manual case-insensitive equality keeps this constexpr (std::tolower
    // is locale-aware and not constant-evaluable).
    auto eq_ci = [](std::string_view a, std::string_view b) constexpr {
      if (a.size() != b.size()) return false;
      for (size_t i = 0; i < a.size(); ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (ca != cb) return false;
      }
      return true;
    };
    auto lower = [](char c) constexpr -> char {
      return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    };
    auto is_dec_digit = [](char c) constexpr -> bool { return c >= '0' && c <= '9'; };

    if (eq_ci(orig_txt, "true"))  return create_bool(true);
    if (eq_ci(orig_txt, "false")) return create_bool(false);

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

        if (sel_ch == 'x')           { shift_mode = 4; ++skip_chars; }
        else if (sel_ch == 'b')      { shift_mode = 1; ++skip_chars; }
        else if (sel_ch == 'd')      { shift_mode = 10; ++skip_chars; }
        else if (is_dec_digit(sel_ch)) { shift_mode = 10; }
        else if (sel_ch == 'o')      { shift_mode = 3; ++skip_chars; }
        else {
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
        if (c == '_') continue;
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
  Slop add_op(const Slop &other) const {
    Slop result;
    if constexpr (n_words == 1) {
      result.base_[0] = base_[0] + other.base_[0];
      if (extra_[0] == 0 && other.extra_[0] == 0) {
        result.extra_[0] = 0;
      } else {
        result.extra_[0] = extra_[0] | other.extra_[0];
        result.base_[0] |= result.extra_[0];
      }
    } else {
      Blop::add<n_words>(result.base_, base_, other.base_);
      if (!has_extra() && !other.has_extra()) {
        result.extra_ = zero_array();
      } else {
        Blop::bor<n_words>(result.extra_, extra_, other.extra_);
        Blop::bor<n_words>(result.base_, result.base_, result.extra_);
      }
    }
    return result;
  }

  Slop sub_op(const Slop &other) const {
    return add_op(other.neg_op());
  }

  Slop mult_op(const Slop &other) const {
    Slop result;
    if (has_extra() || other.has_extra()) {
      // Conservative: all bits unknown
      int nbits = get_bits() + other.get_bits();
      bool neg1 = is_negative();
      bool neg2 = other.is_negative();
      if (neg1 != neg2) return unknown_negative(std::min(nbits, N));
      return unknown_positive(std::min(nbits, N));
    }
    Blop::mult<n_words>(result.base_, base_, other.base_);
    result.extra_ = zero_array();
    return result;
  }

  Slop div_op(const Slop &other) const {
    if (other.is_known_false()) {
      if (is_negative()) return unknown_negative(2);
      return unknown_positive(2);
    }
    if (has_extra() || other.has_extra()) {
      int b = get_bits();
      if (!other.has_extra()) {
        b -= other.get_bits();
        if (b <= 0) return Slop(0);
      }
      bool neg1 = is_negative();
      bool neg2 = other.is_negative();
      if (neg1 != neg2) return unknown_negative(b);
      return unknown_positive(b);
    }
    Slop result;
    Blop::div<n_words>(result.base_, base_, other.base_);
    result.extra_ = zero_array();
    return result;
  }

  Slop neg_op() const {
    Slop result;
    Blop::neg<n_words>(result.base_, base_);
    if (has_extra()) {
      result.extra_ = extra_;
      Blop::bor<n_words>(result.base_, result.base_, result.extra_);
    } else {
      result.extra_ = zero_array();
    }
    return result;
  }

  // --- Bitwise ---
  Slop or_op(const Slop &other) const {
    Slop result;
    if (!has_extra() && !other.has_extra()) {
      Blop::bor<n_words>(result.base_, base_, other.base_);
      result.extra_ = zero_array();
    } else {
      for (int i = 0; i < n_words; ++i) {
        int64_t known1_a = base_[i] & ~extra_[i];
        int64_t known1_b = other.base_[i] & ~other.extra_[i];
        int64_t known0_a = ~base_[i];
        int64_t known0_b = ~other.base_[i];
        int64_t result_known1 = known1_a | known1_b;
        int64_t result_known0 = known0_a & known0_b;
        result.extra_[i] = ~result_known1 & ~result_known0;
        result.base_[i]  = result_known1 | result.extra_[i];
      }
    }
    return result;
  }

  Slop and_op(const Slop &other) const {
    Slop result;
    if (!has_extra() && !other.has_extra()) {
      Blop::band<n_words>(result.base_, base_, other.base_);
      result.extra_ = zero_array();
    } else {
      for (int i = 0; i < n_words; ++i) {
        int64_t known0_a = ~base_[i];
        int64_t known0_b = ~other.base_[i];
        int64_t known1_a = base_[i] & ~extra_[i];
        int64_t known1_b = other.base_[i] & ~other.extra_[i];
        int64_t result_known0 = known0_a | known0_b;
        int64_t result_known1 = known1_a & known1_b;
        result.extra_[i] = ~result_known0 & ~result_known1;
        result.base_[i]  = result_known1 | result.extra_[i];
      }
    }
    return result;
  }

  Slop xor_op(const Slop &other) const {
    Slop result;
    Blop::bxor<n_words>(result.base_, base_, other.base_);
    if (!has_extra() && !other.has_extra()) {
      result.extra_ = zero_array();
    } else {
      Blop::bor<n_words>(result.extra_, extra_, other.extra_);
      Blop::bor<n_words>(result.base_, result.base_, result.extra_);
    }
    return result;
  }

  Slop not_op() const {
    Slop result;
    Blop::bnot<n_words>(result.base_, base_);
    if (!has_extra()) {
      result.extra_ = zero_array();
    } else {
      result.extra_ = extra_;
      Blop::bor<n_words>(result.base_, result.base_, result.extra_);
    }
    return result;
  }

  // --- Shift ---
  Slop lsh_op(int64_t amount) const {
    if (amount == 0) return *this;
    Slop result;
    Blop::shl<n_words>(result.base_, base_, amount);
    if (has_extra()) {
      Blop::shl<n_words>(result.extra_, extra_, amount);
    } else {
      result.extra_ = zero_array();
    }
    return result;
  }

  Slop rsh_op(int64_t amount) const {
    if (amount == 0) return *this;
    Slop result;
    Blop::shr<n_words>(result.base_, base_, amount);
    if (has_extra()) {
      Blop::shr<n_words>(result.extra_, extra_, amount);
    } else {
      result.extra_ = zero_array();
    }
    return result;
  }

  // --- Comparison ---
  Slop eq_op(const Slop &other) const {
    if (has_extra() || other.has_extra()) return unknown(1);
    return create_bool(Blop::eq<n_words>(base_, other.base_));
  }

  bool operator==(const Slop &other) const {
    if (has_extra() || other.has_extra()) return false;
    return Blop::eq<n_words>(base_, other.base_);
  }
  bool operator!=(const Slop &other) const { return !(*this == other); }
  bool operator<(const Slop &other) const  { return Blop::lt<n_words>(base_, other.base_); }
  bool operator<=(const Slop &other) const { return !(other < *this); }
  bool operator>(const Slop &other) const  { return other < *this; }
  bool operator>=(const Slop &other) const { return !(*this < other); }

  // --- Bit manipulation ---
  Slop sext_op(int from_bit) const {
    Slop result;
    Blop::sext<n_words>(result.base_, base_, from_bit);
    result.extra_ = extra_;
    return result;
  }

  Slop get_mask_op() const {
    if (!is_negative()) return *this;
    Slop result;
    int nbits = get_bits();
    result.base_ = base_;
    int top_word = nbits / 64;
    int top_bit  = nbits % 64;
    if (top_bit > 0 && top_word < n_words) {
      result.base_[top_word] &= (int64_t(1) << top_bit) - 1;
    }
    for (int i = top_word + 1; i < n_words; ++i) {
      result.base_[i] = 0;
    }
    result.extra_ = zero_array();
    return result;
  }

  // get_mask_op(mask): copy the bits selected by `mask` into a new integer,
  // packed LSB-first in their original order. Negative mask = "everything
  // except the lowest |mask| bits". Mirrors Lconst::get_mask_op semantics for
  // the non-string, non-unknown path.
  Slop get_mask_op(const Slop &mask) const {
    if (mask.has_unknowns()) return invalid();

    bool mask_neg  = mask.is_negative();
    int  mask_bits = mask.get_bits();
    int  positive_mask_bits = mask_neg ? (mask_bits - 1) : mask_bits;
    int  src_bits  = get_bits();

    Slop result;
    int  out_bit = 0;
    for (int i = 0; i < positive_mask_bits; ++i) {
      bool selected = mask_neg ? !mask.bit_test(i) : mask.bit_test(i);
      if (!selected) continue;
      bool b = (i < src_bits) && bit_test(i);
      if (b) {
        int word = out_bit / 64;
        int bit  = out_bit % 64;
        if (word < n_words) result.base_[word] |= int64_t(1) << bit;
      }
      ++out_bit;
    }
    if (mask_neg) {
      for (int i = positive_mask_bits; i < src_bits; ++i) {
        if (bit_test(i)) {
          int word = out_bit / 64;
          int bit  = out_bit % 64;
          if (word < n_words) result.base_[word] |= int64_t(1) << bit;
        }
        ++out_bit;
      }
    }
    return result;
  }

  // set_mask_op(mask, value): replace the bits selected by `mask` with bits
  // taken LSB-first from `value`; bits not selected stay unchanged. Mirrors
  // Lconst::set_mask_op for the non-string, non-unknown path.
  Slop set_mask_op(const Slop &mask, const Slop &value) const {
    if (mask.is_known_false()) return *this;
    assert(!mask.has_unknowns());

    bool mask_neg = mask.is_negative();
    int  mask_bits = mask.get_bits();
    int  positive_mask_bits = mask_neg ? (mask_bits - 1) : mask_bits;

    int src_bits = get_bits();
    int val_bits = value.get_bits();
    int out_bits = std::max(src_bits, mask_bits);
    if (mask_neg) out_bits = std::max(out_bits, positive_mask_bits + val_bits);
    if (out_bits > N) out_bits = N;

    Slop result;
    int value_pos = 0;
    for (int i = 0; i < out_bits; ++i) {
      bool from_value;
      if (i < positive_mask_bits) {
        bool mb = mask.bit_test(i);
        from_value = mask_neg ? !mb : mb;
      } else {
        from_value = mask_neg;
      }
      bool the_bit;
      if (from_value) {
        the_bit = value.bit_test(value_pos);
        ++value_pos;
      } else {
        the_bit = bit_test(i);
      }
      if (the_bit) {
        int word = i / 64;
        int bit  = i % 64;
        if (word < n_words) result.base_[word] |= int64_t(1) << bit;
      }
    }
    return result;
  }

  // ror_op: OR-reduction with another operand to a single bit (1 if either
  // side has any nonzero bit). Matches Lconst::ror_op.
  Slop ror_op(const Slop &other) const {
    bool any = is_known_true() || other.is_known_true();
    return Slop(any ? int64_t(1) : int64_t(0));
  }

  Slop concat_op(const Slop &other) const {
    int other_bits = other.get_bits();
    if (other_bits <= 0) return *this;
    auto shifted = lsh_op(other_bits);
    auto masked = other.get_mask_op();
    return shifted.or_op(masked);
  }

  Slop adjust_bits(int amount) const {
    assert(amount > 0);
    Slop result;
    result.base_ = base_;
    result.extra_ = extra_;
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
  bool is_negative() const { return Blop::is_negative<n_words>(base_); }
  bool is_positive() const { return !is_negative(); }
  bool has_unknowns() const { return has_extra(); }
  bool is_known_false() const {
    if (has_extra()) return false;
    return Blop::is_zero<n_words>(base_);
  }
  bool is_known_true() const {
    if (has_extra()) {
      // Any known 1 bit?
      for (int i = 0; i < n_words; ++i) {
        if ((base_[i] & ~extra_[i]) != 0) return true;
      }
      return false;
    }
    return !Blop::is_zero<n_words>(base_);
  }
  bool is_invalid() const { return type_ == Type::Invalid; }
  // is_ref: encoded as Type::Invalid carrying a non-zero packed-string
  // payload — mirrors Lconst::is_ref. `invalid()` (no value) has all-zero
  // base; `from_ref` keeps the same Invalid tag but stores bytes there.
  bool is_ref() const {
    if (type_ != Type::Invalid) return false;
    for (int i = 0; i < n_words; ++i) {
      if (base_[i] != 0) return true;
    }
    return false;
  }
  bool is_integer() const { return type_ == Type::Integer; }
  bool is_string() const  { return type_ == Type::String; }

  bool is_mask() const {
    if (has_extra() || is_negative()) return false;
    if constexpr (n_words == 1) {
      return base_[0] > 0 && ((base_[0] + 1) & base_[0]) == 0;
    } else {
      int top = n_words - 1;
      while (top > 0 && base_[top] == 0) --top;
      if (base_[top] <= 0) return false;
      if (((base_[top] + 1) & base_[top]) != 0) return false;
      for (int i = 0; i < top; ++i) {
        if (base_[i] != -1) return false;
      }
      return true;
    }
  }

  bool is_power2() const {
    if (has_extra() || is_negative()) return false;
    if constexpr (n_words == 1) {
      return base_[0] > 0 && ((base_[0] - 1) & base_[0]) == 0;
    } else {
      int nonzero_count = 0;
      int nonzero_idx = -1;
      for (int i = 0; i < n_words; ++i) {
        if (base_[i] != 0) { ++nonzero_count; nonzero_idx = i; }
      }
      if (nonzero_count != 1) return false;
      return ((base_[nonzero_idx] - 1) & base_[nonzero_idx]) == 0;
    }
  }

  constexpr int get_bits() const { return Blop::get_bits<n_words>(base_); }

  bool bit_test(int pos) const {
    int word = pos / 64;
    int bit  = pos % 64;
    if (word >= n_words) return base_[n_words - 1] < 0;
    return (base_[word] >> bit) & 1;
  }

  int get_first_bit_set() const {
    auto c = Blop::ctz<n_words>(base_);
    return (c >= n_words * 64) ? -1 : c;
  }

  int get_last_bit_set() const { return Blop::msb<n_words>(base_); }

  int popcount() const { return Blop::popcount<n_words>(base_); }

  int get_trailing_zeroes() const {
    if (is_known_false()) return 0;
    return Blop::ctz<n_words>(base_);
  }

  constexpr bool is_i() const {
    if (has_extra()) return false;
    return get_bits() <= 62;
  }

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
          if (ch == 0 && w == n_words - 1) break;
          str.push_back(ch);
          tmp >>= 8;
        }
      }
      while (!str.empty() && str.back() == '\0') str.pop_back();
    }
    return str;
  }

  std::string to_binary() const {
    int nbits = get_bits();
    if (nbits <= 0) return "0";

    std::string result;
    for (int i = nbits - 1; i >= 0; --i) {
      int word = i / 64;
      int bit  = i % 64;
      bool is_unk = (word < n_words) ? ((extra_[word] >> bit) & 1) : false;
      if (is_unk) {
        result.push_back('?');
      } else {
        result.push_back(bit_test(i) ? '1' : '0');
      }
    }
    return result;
  }

  std::string to_pyrope() const {
    if (is_invalid()) return "";

    if (type_ == Type::String) {
      auto str = to_string();
      if (str.empty()) return "''";
      return std::format("'{}'", str);
    }

    if (type_ == Type::Boolean) {
      return is_known_true() ? "true" : "false";
    }

    if (has_extra()) {
      auto bin = to_binary();
      if (is_negative()) return std::format("0sb{}", bin);
      return std::format("0b{}", bin);
    }

    if (is_i()) {
      int64_t val = to_i();
      if (val >= -63 && val <= 63) return std::to_string(val);
      if (val < 0) return std::format("-0x{:x}", -val);
      return std::format("0x{:x}", val);
    }

    // Multi-word hex
    if (is_negative()) {
      auto pos = neg_op();
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
    if (is_known_false()) return "'sb0";
    if (has_extra()) {
      return std::format("{}'sb{}", get_bits(), to_binary());
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
      if (i == n_words - 1) hex += std::format("{:x}", static_cast<uint64_t>(base_[i]));
      else hex += std::format("{:016x}", static_cast<uint64_t>(base_[i]));
    }
    return std::format("{}'sh{}", nbits, hex);
  }

  // --- Debug ---
  void dump() const {
    std::print("Slop<{}> base:0x", N);
    for (int i = n_words - 1; i >= 0; --i) {
      std::print("_{:016x}", static_cast<uint64_t>(base_[i]));
    }
    std::print("\n        extra:0x");
    for (int i = n_words - 1; i >= 0; --i) {
      std::print("_{:016x}", static_cast<uint64_t>(extra_[i]));
    }
    std::print("\n");
  }
};
