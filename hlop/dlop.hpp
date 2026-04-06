//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "blop.hpp"
#include "raw_ptr_pool.hpp"
#include "spool_ptr.hpp"

class Dlop {
private:
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

  enum class Type : int16_t {
    Invalid  = -1,
    Integer  = 0,
    Boolean  = 1,
    String   = 2,
    Bitwidth = 3
  };

  static inline thread_local std::vector<raw_ptr_pool *> free_pool;

public:
  static void     free(size_t sz, int64_t *ptr);
  static int64_t *alloc(size_t sz);

  Type    type;
  int16_t size;  // bucket size in 64-bit words

  int64_t *base;   // value bits (unknowns have base bit = 1)
  int64_t *extra;  // unknown mask (1 = unknown, 0 = known). Invariant: base == (base | extra)

  int64_t data[2];  // inline storage for size==1 (avoids pool alloc)

  // --- Internal helpers for building values ---
  void shl_base(int64_t amt) { Blop::shln(base, size, base, amt); }
  void shl_extra(int64_t amt) { Blop::shln(extra, size, extra, amt); }

  void mult_base(int64_t v) {
    if (size == 1) {
      base[0] *= v;
    } else {
      int64_t *tmp = alloc(size);
      memcpy(tmp, base, size * sizeof(int64_t));
      Blop::multn(base, size, tmp, size, v);
      free(size, tmp);
    }
  }

  void extend_base(int64_t v) { Blop::extend(base, size, v); }
  void extend_extra(int64_t v) { Blop::extend(extra, size, v); }

  void add_base(int64_t v) {
    if (size == 1) {
      base[0] += v;
    } else {
      int64_t *tmp = alloc(size);
      Blop::extend(tmp, size, v);
      Blop::addn(base, size, base, tmp);
      free(size, tmp);
    }
  }

  void or_base(int64_t v) {
    if (size == 1) {
      base[0] |= v;
    } else {
      Blop::orn(base, size, base, v);
    }
  }

  void or_extra(int64_t v) {
    if (size == 1) {
      extra[0] |= v;
    } else {
      Blop::orn(extra, size, extra, v);
    }
  }

  void negate_mut() {
    assert(type == Type::Integer);
    if (size == 1) {
      base[0] = -base[0];
    } else {
      Blop::negn(base, size, base);
    }
  }

  void clear() {
    for (int i = 0; i < size; ++i) {
      base[i]  = 0;
      extra[i] = 0;
    }
  }

  void reconstruct(Type tp, size_t sz) {
    type = tp;
    size = sz;
    if (sz > 1) {
      base  = alloc(sz);
      extra = alloc(sz);
      clear();
    } else {
      base    = &data[0];
      extra   = &data[1];
      data[0] = 0;
      data[1] = 0;
    }
  }

  // Ensure both operands have same size, growing if needed
  void grow_to(int16_t new_size);
  // Shrink to minimum needed words
  void normalize();
  // Grow result to hold at least 'needed' words
  static spool_ptr<Dlop> make_result(Type tp, int16_t sz);

  friend spool_ptr<Dlop>;
  friend spool_ptr_pool<Dlop>;
  uint32_t shared_count;

  // Align operand sizes for binary operations
  static void align_sizes(spool_ptr<Dlop> &a, spool_ptr<Dlop> &b);

  bool has_extra() const {
    for (int i = 0; i < size; ++i) {
      if (extra[i] != 0) return true;
    }
    return false;
  }

public:
  Dlop() : type(Type::Invalid), size(0), base(nullptr), extra(nullptr) {}

  ~Dlop() {
    if (base && size > 1) {
      assert(extra);
      free(size, base);
      free(size, extra);
    }
  }

  // --- Type queries ---
  bool is_integer() const { return type == Type::Integer; }
  bool is_bool() const { return type == Type::Boolean; }
  bool is_string() const { return type == Type::String; }
  bool is_invalid() const { return type == Type::Invalid; }

  // --- Factory methods ---
  static spool_ptr<Dlop> create_bool(bool val);
  static spool_ptr<Dlop> create_integer(int64_t val);
  static spool_ptr<Dlop> create_string(std::string_view txt);
  static spool_ptr<Dlop> from_binary(std::string_view txt, bool unsigned_result);
  static spool_ptr<Dlop> from_pyrope(std::string_view orig_txt);
  static spool_ptr<Dlop> from_string(std::string_view txt);
  static spool_ptr<Dlop> invalid();

  static spool_ptr<Dlop> unknown(int nbits);
  static spool_ptr<Dlop> unknown_positive(int nbits);
  static spool_ptr<Dlop> unknown_negative(int nbits);

  // --- Mutating arithmetic ---
  void mut_add(spool_ptr<Dlop> other);
  void mut_add(int64_t other);

  // --- Arithmetic operations ---
  spool_ptr<Dlop> add_op(spool_ptr<Dlop> other) const;
  spool_ptr<Dlop> add_op(int64_t other) const;
  spool_ptr<Dlop> sub_op(spool_ptr<Dlop> other) const;
  spool_ptr<Dlop> sub_op(int64_t other) const;
  spool_ptr<Dlop> mult_op(spool_ptr<Dlop> other) const;
  spool_ptr<Dlop> div_op(spool_ptr<Dlop> other) const;
  spool_ptr<Dlop> neg_op() const;

  // --- Bitwise operations ---
  spool_ptr<Dlop> or_op(spool_ptr<Dlop> other) const;
  spool_ptr<Dlop> and_op(spool_ptr<Dlop> other) const;
  spool_ptr<Dlop> xor_op(spool_ptr<Dlop> other) const;
  spool_ptr<Dlop> not_op() const;

  // --- Shift operations ---
  spool_ptr<Dlop> lsh_op(int64_t amount) const;
  spool_ptr<Dlop> rsh_op(int64_t amount) const;

  // --- Comparison operations ---
  spool_ptr<Dlop> eq_op(spool_ptr<Dlop> other) const;
  bool operator==(const Dlop &other) const;
  bool operator!=(const Dlop &other) const;
  bool operator<(const Dlop &other) const;
  bool operator<=(const Dlop &other) const;
  bool operator>(const Dlop &other) const;
  bool operator>=(const Dlop &other) const;

  // --- Bit manipulation ---
  spool_ptr<Dlop> sext_op(int bits) const;
  spool_ptr<Dlop> get_mask_op() const;
  spool_ptr<Dlop> concat_op(spool_ptr<Dlop> other) const;
  spool_ptr<Dlop> adjust_bits(int amount) const;

  // --- Queries ---
  bool is_negative() const;
  bool is_positive() const;
  bool has_unknowns() const { return has_extra(); }
  bool is_known_false() const;
  bool is_known_true() const;
  bool is_mask() const;
  bool is_power2() const;
  int  get_bits() const;
  bool bit_test(int pos) const;
  int  get_first_bit_set() const;
  int  get_last_bit_set() const;
  int  popcount() const;
  int  get_trailing_zeroes() const;
  bool is_i() const;
  int64_t to_i() const;

  // --- Conversion ---
  std::string to_pyrope() const;
  std::string to_binary() const;
  std::string to_verilog() const;
  std::string to_string() const;

  void dump() const;
};
