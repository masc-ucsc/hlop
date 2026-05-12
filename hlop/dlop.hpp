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
    Bitwidth = 3,
    // Nil is Pyrope's tagged unit ("absence of value") — distinct from Invalid
    // (which means error / unset). All arithmetic and logical ops propagate Nil:
    // any binary op with a Nil operand returns Nil.
    Nil      = 4
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
  // Note: Dlop::from_pyrope cannot be constexpr — Dlop bodies live in a
  // thread-local pool (raw_ptr_pool) and acquire storage at runtime. For
  // compile-time literals use Slop<N>::from_pyrope, which folds at the
  // call site and stays in the static stack-allocated path.
  static spool_ptr<Dlop> from_pyrope(std::string_view orig_txt);
  static spool_ptr<Dlop> from_string(std::string_view txt);
  static spool_ptr<Dlop> from_ref(std::string_view txt);
  static spool_ptr<Dlop> invalid();

  static spool_ptr<Dlop> unknown(int nbits);
  static spool_ptr<Dlop> unknown_positive(int nbits);
  static spool_ptr<Dlop> unknown_negative(int nbits);

  // Pyrope nil literal — distinct from invalid()
  static spool_ptr<Dlop> nil();

  // Mask helpers (statics): (1<<bits)-1, contiguous slice [l..h], and ~((1<<bits)-1)
  static spool_ptr<Dlop> get_mask_value(int bits);
  static spool_ptr<Dlop> get_mask_value(int h, int l);
  static spool_ptr<Dlop> get_neg_mask_value(int bits);

  // Persistence: stable binary roundtrip. Layout (little-endian word order):
  //   [1 B] type, [2 B] size, [size * 8 B] base words, [size * 8 B] extra words
  std::string                  serialize() const;
  static spool_ptr<Dlop>       unserialize(std::string_view v);

  uint64_t hash() const;

protected:
  // --- Mutating arithmetic ---
  void mut_add(spool_ptr<Dlop> other);
  void mut_add(int64_t other);

public:
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

  // --- Reduction operations ---
  // ror_op: OR-reduction with another operand (1-bit result, 1 if any nonzero).
  spool_ptr<Dlop> ror_op(spool_ptr<Dlop> other) const;

  // --- Bit manipulation ---
  spool_ptr<Dlop> sext_op(int bits) const;
  spool_ptr<Dlop> get_mask_op() const;
  spool_ptr<Dlop> get_mask_op(spool_ptr<Dlop> mask) const;
  spool_ptr<Dlop> set_mask_op(spool_ptr<Dlop> mask, spool_ptr<Dlop> value) const;
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
  bool is_nil() const { return type == Type::Nil; }
  // is_ref: encoded as Type::Invalid carrying a non-zero packed-string payload.
  // Invalid (no value) has Type::Invalid + zero/empty content; from_ref keeps
  // the same Invalid tag but stores the byte-packed identifier in `base`.
  bool is_ref() const;

  // Bitwidth slice helpers (instance form): walk contiguous 1-runs in base.
  // Returns (-1,-1) when no contiguous range exists (or empty for pairs).
  spool_ptr<Dlop>                  get_mask_value() const;
  std::vector<std::pair<int, int>> get_mask_range_pairs() const;
  std::pair<int, int>              get_mask_range() const;

  // Pyrope tuple-field stringification (subset of to_pyrope allowed as a field key)
  std::string to_field() const;

  // Resolve unknown bits to fresh random known bits (deterministic per-process seed)
  spool_ptr<Dlop> to_known_rand() const;
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

#include <format>

// std::format integration. Renders via to_pyrope() for both the value-type
// and the pool-pointer wrapper, so call sites can write:
//   std::format("{}", dlop_value)
//   std::format("{}", spool_ptr_to_dlop)
template <>
struct std::formatter<Dlop> : formatter<string_view> {
  template <typename FormatContext>
  auto format(const Dlop& d, FormatContext& ctx) const {
    return formatter<string_view>::format(d.to_pyrope(), ctx);
  }
};

template <>
struct std::formatter<spool_ptr<Dlop>> : formatter<string_view> {
  template <typename FormatContext>
  auto format(const spool_ptr<Dlop>& d, FormatContext& ctx) const {
    if (!d) {
      return formatter<string_view>::format(std::string_view{}, ctx);
    }
    return formatter<string_view>::format(d->to_pyrope(), ctx);
  }
};
