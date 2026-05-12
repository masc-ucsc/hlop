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

  // Layout: type+size+shared_count packs into 8 bytes; storage union is 16 bytes.
  // For size <= 1 the value bits live inline in `data[]`; for size > 1 the
  // pool-allocated buffers live in `big.bp` / `big.ep`. `shared_count` is touched
  // only by spool_ptr<Dlop>; embedded Dlops never use it.
  Type     type;
  int16_t  size;          // bucket size in 64-bit words
  uint32_t shared_count;  // touched only by spool_ptr_pool<Dlop>
  union {
    int64_t data[2];                       // size <= 1: data[0]=base word, data[1]=extra word
    struct { int64_t *bp; int64_t *ep; } big;  // size  > 1: pool buffers
  };

  // --- Storage accessors ---
  // base()/extra() return the value-bit / unknown-bit word pointer regardless
  // of inline vs pool storage. One predictable branch; the compiler hoists it
  // out of loops in most callers.
  int64_t       *base()        noexcept { return size > 1 ? big.bp : &data[0]; }
  int64_t       *extra()       noexcept { return size > 1 ? big.ep : &data[1]; }
  const int64_t *base()  const noexcept { return size > 1 ? big.bp : &data[0]; }
  const int64_t *extra() const noexcept { return size > 1 ? big.ep : &data[1]; }

  // Release pool-allocated word buffers, if any. Leaves size unchanged; the
  // caller is expected to reset size/type next (or destruct).
  void free_storage() noexcept {
    if (size > 1) {
      free(size, big.bp);
      free(size, big.ep);
    }
  }

  // --- Internal helpers for building values ---
  void shl_base(int64_t amt) { Blop::shln(base(), size, base(), amt); }
  void shl_extra(int64_t amt) { Blop::shln(extra(), size, extra(), amt); }

  void mult_base(int64_t v) {
    if (size == 1) {
      data[0] *= v;
    } else {
      int64_t *tmp = alloc(size);
      memcpy(tmp, big.bp, size * sizeof(int64_t));
      Blop::multn(big.bp, size, tmp, size, v);
      free(size, tmp);
    }
  }

  void extend_base(int64_t v) { Blop::extend(base(), size, v); }
  void extend_extra(int64_t v) { Blop::extend(extra(), size, v); }

  void add_base(int64_t v) {
    if (size == 1) {
      data[0] += v;
    } else {
      int64_t *tmp = alloc(size);
      Blop::extend(tmp, size, v);
      Blop::addn(big.bp, size, big.bp, tmp);
      free(size, tmp);
    }
  }

  void or_base(int64_t v) {
    if (size == 1) {
      data[0] |= v;
    } else {
      Blop::orn(big.bp, size, big.bp, v);
    }
  }

  void or_extra(int64_t v) {
    if (size == 1) {
      data[1] |= v;
    } else {
      Blop::orn(big.ep, size, big.ep, v);
    }
  }

  void negate_mut() {
    assert(type == Type::Integer);
    if (size == 1) {
      data[0] = -data[0];
    } else {
      Blop::negn(big.bp, size, big.bp);
    }
  }

  void clear() {
    if (size <= 1) {
      data[0] = 0;
      data[1] = 0;
    } else {
      for (int i = 0; i < size; ++i) {
        big.bp[i] = 0;
        big.ep[i] = 0;
      }
    }
  }

  void reconstruct(Type tp, size_t sz) {
    free_storage();
    type = tp;
    size = static_cast<int16_t>(sz);
    if (sz > 1) {
      big.bp = alloc(sz);
      big.ep = alloc(sz);
      for (size_t i = 0; i < sz; ++i) {
        big.bp[i] = 0;
        big.ep[i] = 0;
      }
    } else {
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

  // Align operand sizes for binary operations
  static void align_sizes(spool_ptr<Dlop> &a, spool_ptr<Dlop> &b);

  bool has_extra() const {
    if (size <= 1) return data[1] != 0;
    for (int i = 0; i < size; ++i) {
      if (big.ep[i] != 0) return true;
    }
    return false;
  }

  // Signed less-than on base words. Callers must guarantee both sides are
  // fully known (no unknown bits); used by the three-valued lt/le/gt/ge ops
  // after they short-circuit on unknowns. Not exposed as operator< because
  // hiding unknown-propagation behind `a < b` invites silent miscompares.
  bool cmp_less_known(const Dlop &other) const;

public:
  Dlop() noexcept : type(Type::Invalid), size(0), shared_count(0), data{0, 0} {}

  ~Dlop() { free_storage(); }

  // Embedded Dlops can be moved cheaply; the source is left empty so its
  // destructor is a no-op.
  Dlop(Dlop&& o) noexcept : type(o.type), size(o.size), shared_count(0) {
    if (size > 1) {
      big = o.big;
    } else {
      data[0] = o.data[0];
      data[1] = o.data[1];
    }
    o.size = 0;
    o.type = Type::Invalid;
  }

  Dlop& operator=(Dlop&& o) noexcept {
    if (this != &o) {
      free_storage();
      type = o.type;
      size = o.size;
      if (size > 1) {
        big = o.big;
      } else {
        data[0] = o.data[0];
        data[1] = o.data[1];
      }
      o.size = 0;
      o.type = Type::Invalid;
    }
    return *this;
  }

  // Deep copy: each Dlop owns its own word buffers. spool_ptr<Dlop> handles
  // ref-counted sharing separately.
  Dlop(const Dlop& o) : type(o.type), size(o.size), shared_count(0) {
    if (size > 1) {
      big.bp = alloc(size);
      big.ep = alloc(size);
      for (int i = 0; i < size; ++i) {
        big.bp[i] = o.big.bp[i];
        big.ep[i] = o.big.ep[i];
      }
    } else {
      data[0] = o.data[0];
      data[1] = o.data[1];
    }
  }

  Dlop& operator=(const Dlop& o) {
    if (this != &o) {
      Dlop tmp(o);
      *this = std::move(tmp);
    }
    return *this;
  }

  // Convenience: assign the contents of a pool-managed spool_ptr<Dlop> into an
  // embedded Dlop. The rvalue overload moves out of the pool object (leaving
  // it size==0, Type::Invalid) so the spool_ptr's destructor returns an empty
  // slot to the pool. Lets embedded callers write:
  //   Dlop total; ...
  //   total = total.add_op(v);   // op returns spool_ptr; this moves it in
  Dlop& operator=(spool_ptr<Dlop>&& sp) noexcept {
    *this = std::move(*sp);
    return *this;
  }
  Dlop& operator=(const spool_ptr<Dlop>& sp) {
    *this = *sp;
    return *this;
  }

  // --- Type queries ---
  bool is_integer() const { return type == Type::Integer; }
  bool is_bool() const { return type == Type::Boolean; }
  bool is_string() const { return type == Type::String; }
  bool is_invalid() const { return type == Type::Invalid; }

  // --- In-place initializers (no spool_ptr round-trip) ---
  // These build the value directly into `*this`. Use them when a Dlop is
  // embedded in another struct, or via the `create_*` / `from_*` wrappers
  // when a pool-managed spool_ptr<Dlop> is needed.
  void init_bool(bool val);
  void init_integer(int64_t val);
  void init_string(std::string_view txt);
  void init_from_binary(std::string_view txt, bool unsigned_result);
  void init_from_pyrope(std::string_view orig_txt);
  void init_from_string(std::string_view txt) { init_string(txt); }
  void init_from_ref(std::string_view txt);
  void init_invalid();
  void init_nil();
  void init_unknown(int nbits);
  void init_unknown_positive(int nbits);
  void init_unknown_negative(int nbits);

  // --- Factory methods (wrap an init_* into a spool_ptr<Dlop>) ---
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
  // Canonical: const Dlop&. spool_ptr overload is a thin wrapper.
  void mut_add(const Dlop& other);
  void mut_add(spool_ptr<Dlop> other) { mut_add(*other); }
  void mut_add(int64_t other);

public:
  // --- Arithmetic operations ---
  // Every binary op accepts (const Dlop&) or (spool_ptr<Dlop>); the spool
  // overload is a one-liner that delegates to the reference form, so embedded
  // Dlops and pool-managed spool_ptr<Dlop>s can mix freely without copies.
  spool_ptr<Dlop> add_op(const Dlop& other) const;
  spool_ptr<Dlop> add_op(spool_ptr<Dlop> other) const { return add_op(*other); }
  spool_ptr<Dlop> add_op(int64_t other) const;
  spool_ptr<Dlop> sub_op(const Dlop& other) const;
  spool_ptr<Dlop> sub_op(spool_ptr<Dlop> other) const { return sub_op(*other); }
  spool_ptr<Dlop> sub_op(int64_t other) const;
  spool_ptr<Dlop> mult_op(const Dlop& other) const;
  spool_ptr<Dlop> mult_op(spool_ptr<Dlop> other) const { return mult_op(*other); }
  spool_ptr<Dlop> div_op(const Dlop& other) const;
  spool_ptr<Dlop> div_op(spool_ptr<Dlop> other) const { return div_op(*other); }
  spool_ptr<Dlop> neg_op() const;

  // --- Bitwise operations ---
  spool_ptr<Dlop> or_op(const Dlop& other) const;
  spool_ptr<Dlop> or_op(spool_ptr<Dlop> other) const { return or_op(*other); }
  spool_ptr<Dlop> and_op(const Dlop& other) const;
  spool_ptr<Dlop> and_op(spool_ptr<Dlop> other) const { return and_op(*other); }
  spool_ptr<Dlop> xor_op(const Dlop& other) const;
  spool_ptr<Dlop> xor_op(spool_ptr<Dlop> other) const { return xor_op(*other); }
  spool_ptr<Dlop> not_op() const;

  // --- Shift operations ---
  spool_ptr<Dlop> lsh_op(int64_t amount) const;
  spool_ptr<Dlop> rsh_op(int64_t amount) const;

  // --- Comparison operations ---
  spool_ptr<Dlop> eq_op(const Dlop& other) const;
  spool_ptr<Dlop> eq_op(spool_ptr<Dlop> other) const { return eq_op(*other); }
  // same_repr: structural compare of base AND extra. Two values with the same
  // unknown pattern (e.g. 0sb?1 vs 0sb?1) are equal. Use for containers, dedup,
  // and hashing where reflexivity is required.
  bool same_repr(const Dlop &other) const;
  // is_known_eq: three-valued equality collapsed to bool — true only when both
  // sides are fully known and numerically equal; false if either side has any
  // unknown bits. Use in asserts where "definitely equal" is the question.
  bool is_known_eq(const Dlop &other) const;

  // Three-valued comparison ops returning a Bool Dlop (or a 1-bit unknown
  // when either side has unknown bits). Mirror eq_op's unknown handling so
  // pass code can propagate `0sb?` through compares without special-casing.
  spool_ptr<Dlop> lt_op(const Dlop& other) const;
  spool_ptr<Dlop> lt_op(spool_ptr<Dlop> other) const { return lt_op(*other); }
  spool_ptr<Dlop> le_op(const Dlop& other) const;
  spool_ptr<Dlop> le_op(spool_ptr<Dlop> other) const { return le_op(*other); }
  spool_ptr<Dlop> gt_op(const Dlop& other) const;
  spool_ptr<Dlop> gt_op(spool_ptr<Dlop> other) const { return gt_op(*other); }
  spool_ptr<Dlop> ge_op(const Dlop& other) const;
  spool_ptr<Dlop> ge_op(spool_ptr<Dlop> other) const { return ge_op(*other); }

  // --- Reduction operations ---
  // ror_op: OR-reduction with another operand (1-bit result, 1 if any nonzero).
  spool_ptr<Dlop> ror_op(const Dlop& other) const;
  spool_ptr<Dlop> ror_op(spool_ptr<Dlop> other) const { return ror_op(*other); }
  // ror_op (unary): OR-reduction over the single operand's bits, returning
  // a Bool Dlop. True if any bit is set (known or unknown); known-false only
  // when every bit is provably zero. Distinct from the binary form above,
  // which returns an Integer Dlop for the Lconst::ror_op semantics.
  spool_ptr<Dlop> ror_op() const;
  // rand_op: AND-reduction (single operand). Returns bool true iff every bit
  // is set (i.e., value is a 2^n-1 mask). Unknown bits → 1-bit unknown.
  spool_ptr<Dlop> rand_op() const;
  // rxor_op: XOR-reduction (single operand). Returns bool true iff popcount
  // is odd. Unknown bits → 1-bit unknown.
  spool_ptr<Dlop> rxor_op() const;

  // --- Bit manipulation ---
  spool_ptr<Dlop> sext_op(int bits) const;
  spool_ptr<Dlop> get_mask_op() const;
  spool_ptr<Dlop> get_mask_op(const Dlop& mask) const;
  spool_ptr<Dlop> get_mask_op(spool_ptr<Dlop> mask) const { return get_mask_op(*mask); }
  spool_ptr<Dlop> set_mask_op(const Dlop& mask, const Dlop& value) const;
  spool_ptr<Dlop> set_mask_op(spool_ptr<Dlop> mask, spool_ptr<Dlop> value) const {
    return set_mask_op(*mask, *value);
  }
  spool_ptr<Dlop> concat_op(const Dlop& other) const;
  spool_ptr<Dlop> concat_op(spool_ptr<Dlop> other) const { return concat_op(*other); }
  spool_ptr<Dlop> adjust_bits(int amount) const;

  // --- Queries ---
  bool is_negative() const;
  bool is_positive() const;
  bool has_unknowns() const { return has_extra(); }
  bool is_known_false() const;
  bool is_known_true() const;
  // is_known_zero: numeric zero with no unknowns. Stricter than is_known_false
  // (which also accepts nil/invalid/empty-string); use is_known_zero for
  // `value == 0` checks on integer Dlops where the type is known.
  bool is_known_zero() const;
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

// Lock the layout win: type+size+shared_count (8) + union (16) = 24 bytes,
// down from the previous ~40 bytes (extra base/extra pointers).
static_assert(sizeof(Dlop) == 24, "Dlop layout regressed; expected 24-byte object");

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
