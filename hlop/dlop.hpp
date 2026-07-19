//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <span>
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

  enum class Type : int8_t {
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

  // Where the unknown-bit (extra) plane lives. The two dominant cases need no
  // second word: a fully known value has extra == 0 everywhere (Zero), and the
  // fully-unknown family (0sb?, unknown(n), unknown_bool) has extra identical
  // to base (Mirror) thanks to the base == base|extra invariant. Only values
  // mixing known and unknown bits, or wider than one word, pay for pool
  // storage (Heap).
  enum class XKind : uint8_t {
    Zero   = 0,  // extra is implicitly all-zero; base word inline in `data`
    Mirror = 1,  // extra == base (and base != 0); base word inline in `data`
    Heap   = 2,  // combined pool buffer: base = bp[0..size), extra = bp[size..2*size)
  };

  // Per-thread free-list of word buffers, bucketed by size. Intentionally
  // immortal (heap-allocated, never destroyed): ~Dlop returns buffers here, and
  // it runs during thread-local teardown via spool_ptr_pool<Dlop>. Because
  // free_pool is touched (lazily, on the first big Dlop) *after* that pool is
  // constructed, a plain `thread_local` vector is destroyed *before* the pool
  // (reverse construction order), so teardown free() would touch a dead vector
  // — a destruction-order fiasco that surfaced as flaky heap corruption. A
  // function-local thread_local pointer is never registered for destruction, so
  // the vector it owns stays valid for the whole thread lifetime.
  static std::vector<raw_ptr_pool*>& free_pool() {
    static thread_local auto* pool = new std::vector<raw_ptr_pool*>();
    return *pool;
  }

public:
  static void     free(size_t sz, int64_t* ptr);
  static int64_t* alloc(size_t sz);

  // Layout: type+xkind+size+shared_count packs into 8 bytes; the storage union
  // is one more 8-byte slot — either the inline base word (`data`) or the
  // combined pool buffer (`bp`). Invariants:
  //   size > 1  ⇒ xkind == Heap
  //   Mirror    ⇒ size == 1 and data != 0
  //   Heap      ⇒ size >= 1 and bp was alloc(2*size)
  // `shared_count` is touched only by spool_ptr<Dlop>; embedded Dlops never
  // use it.
  Type     type;
  XKind    xkind;
  int16_t  size;          // value width in 64-bit words
  uint32_t shared_count;  // touched only by spool_ptr_pool<Dlop>
  union {
    int64_t  data;  // xkind != Heap: the base word (extra is implicit)
    int64_t* bp;    // xkind == Heap: base words, then extra words
  };

  // Shared read-only word for the Zero extra plane of inline values.
  inline static constexpr int64_t zero_word = 0;

  bool on_heap() const noexcept { return xkind == XKind::Heap; }

  // --- Storage accessors ---
  // base() is writable in place for every storage kind. extra() is const-only:
  // for inline values it returns the shared zero word (Zero) or aliases base
  // (Mirror). Writers use set_word_pair/set_extra_word (scalar fast paths) or
  // extra_mut (full-plane access, materializes pool storage).
  int64_t*       base() noexcept { return on_heap() ? bp : &data; }
  const int64_t* base() const noexcept { return on_heap() ? bp : &data; }
  const int64_t* extra() const noexcept {
    if (on_heap()) {
      return bp + size;
    }
    return xkind == XKind::Mirror ? &data : &zero_word;
  }

  // Materialize heap storage (combined base+extra buffer) for a size-1 inline
  // value, seeding the extra word with `e`.
  void heapify_set_extra(int64_t e) {
    assert(!on_heap() && size == 1);
    int64_t* p = alloc(2);
    p[0]       = data;
    p[1]       = e;
    bp         = p;
    xkind      = XKind::Heap;
  }

  // Base is about to be mutated in place. Mirror aliases extra to base, so the
  // old extra must be snapshotted into heap storage first; Zero/Heap are safe.
  void unmirror() {
    if (xkind == XKind::Mirror) {
      heapify_set_extra(data);
    }
  }

  // Set the single extra word of a size<=1 value, picking the cheapest
  // storage: Zero, Mirror (e == base), or heap for a mixed pattern. For heap
  // values writes extra word 0 in place. Base must be final before the call.
  void set_extra_word(int64_t e) {
    if (on_heap()) {
      bp[size] = e;
      return;
    }
    if (e == 0) {
      xkind = XKind::Zero;
    } else if (e == data) {
      xkind = XKind::Mirror;
    } else {
      heapify_set_extra(e);
    }
  }

  // Write the base/extra word pair of a size<=1 value (the kernels' scalar
  // fast path), classifying storage.
  void set_word_pair(int64_t b, int64_t e) {
    if (on_heap()) {
      bp[0]    = b;
      bp[size] = e;
      return;
    }
    data  = b;
    xkind = XKind::Zero;
    set_extra_word(e);
  }

  // Writable extra plane, materializing heap storage when needed. Call BEFORE
  // caching base() in result-building loops: materialization moves base from
  // the inline word into the pool buffer.
  int64_t* extra_mut() {
    if (!on_heap()) {
      heapify_set_extra(xkind == XKind::Mirror ? data : 0);
    }
    return bp + size;
  }

  // Zero the whole extra plane.
  void zero_extra() {
    if (on_heap()) {
      memset(bp + size, 0, size * sizeof(int64_t));
    } else {
      xkind = XKind::Zero;
    }
  }

  // Copy the full payload (both planes) from a same-size value.
  void copy_payload_from(const Dlop& o) {
    assert(size == o.size);
    if (on_heap() && o.on_heap()) {
      memcpy(bp, o.bp, 2 * size * sizeof(int64_t));
    } else if (!on_heap() && !o.on_heap()) {
      data  = o.data;
      xkind = o.xkind;
    } else if (o.on_heap()) {  // this inline, o heap (size == 1)
      set_word_pair(o.bp[0], o.bp[1]);
    } else {  // this heap, o inline (size == 1)
      bp[0]    = o.base()[0];
      bp[size] = o.extra()[0];
    }
  }

  // Reclaim inline storage for a size-1 heap value whose extra plane fits the
  // Zero/Mirror encodings. Representation-only: type/size/value unchanged.
  void compact_inline() {
    if (!on_heap() || size > 1) {
      return;
    }
    int64_t b = bp[0];
    int64_t e = bp[1];
    if (e != 0 && e != b) {
      return;  // genuinely mixed known/unknown bits: keep heap storage
    }
    free(2, bp);
    data  = b;
    xkind = (e == 0) ? XKind::Zero : XKind::Mirror;
  }

  // Release the pool buffer, if any. Leaves size/xkind unchanged; the caller
  // is expected to reset size/type/xkind next (or destruct).
  void free_storage() noexcept {
    if (on_heap()) {
      free(2 * size, bp);
    }
  }

  // --- Internal helpers for building values ---
  // The base-mutating helpers unmirror() first: mutating a Mirror-aliased base
  // in place would silently drag extra along with it.
  void shl_base(int64_t amt) {
    unmirror();
    Blop::shln(base(), size, base(), amt);
  }

  void shl_extra(int64_t amt) {
    if (on_heap()) {
      Blop::shln(bp + size, size, bp + size, amt);
      return;
    }
    if (xkind == XKind::Zero) {
      return;  // shifting an all-zero plane
    }
    int64_t e = data;  // Mirror
    Blop::shln(&e, 1, &e, amt);
    set_extra_word(e);
  }

  void mult_base(int64_t v) {
    unmirror();
    if (size == 1) {
      base()[0] *= v;
    } else {
      int64_t* tmp = alloc(size);
      memcpy(tmp, bp, size * sizeof(int64_t));
      Blop::multn(bp, size, tmp, size, v);
      free(size, tmp);
    }
  }

  void extend_base(int64_t v) {
    unmirror();
    Blop::extend(base(), size, v);
  }

  void extend_extra(int64_t v) {
    if (on_heap()) {
      Blop::extend(bp + size, size, v);
    } else {
      set_extra_word(v);
    }
  }

  void add_base(int64_t v) {
    unmirror();
    if (size == 1) {
      base()[0] += v;
    } else {
      int64_t* tmp = alloc(size);
      Blop::extend(tmp, size, v);
      Blop::addn(bp, size, bp, tmp);
      free(size, tmp);
    }
  }

  void or_base(int64_t v) {
    unmirror();
    if (size == 1) {
      base()[0] |= v;
    } else {
      Blop::orn(bp, size, bp, v);
    }
  }

  void or_extra(int64_t v) {
    if (on_heap()) {
      Blop::orn(bp + size, size, bp + size, v);
    } else {
      set_extra_word(extra()[0] | v);
    }
  }

  void negate_mut() {
    assert(type == Type::Integer);
    unmirror();
    if (size == 1) {
      base()[0] = -base()[0];
    } else {
      Blop::negn(bp, size, bp);
    }
  }

  void clear() {
    if (on_heap()) {
      memset(bp, 0, 2 * size * sizeof(int64_t));
    } else {
      data  = 0;
      xkind = XKind::Zero;
    }
  }

  void reconstruct(Type tp, size_t sz) {
    free_storage();
    type = tp;
    size = static_cast<int16_t>(sz);
    if (sz > 1) {
      bp = alloc(2 * sz);
      memset(bp, 0, 2 * sz * sizeof(int64_t));
      xkind = XKind::Heap;
    } else {
      data  = 0;
      xkind = XKind::Zero;
    }
  }

  // Ensure both operands have same size, growing if needed
  void                   grow_to(int16_t new_size);
  // Shrink to minimum needed words
  void                   normalize();
  // Grow result to hold at least 'needed' words
  static spool_ptr<Dlop> make_result(Type tp, int16_t sz);

  friend spool_ptr<Dlop>;
  friend spool_ptr_pool<Dlop>;

  // Align operand sizes for binary operations
  static void align_sizes(spool_ptr<Dlop>& a, spool_ptr<Dlop>& b);

  // Deep copy `src` into a fresh pool-managed Dlop (used by mux_op/hotmux_op
  // to return a selected value without aliasing the caller's storage).
  static spool_ptr<Dlop> clone(const Dlop& src);
  // Ternary-merge candidate values into one Dlop: a bit stays known only when
  // every candidate is known and agrees there; otherwise it becomes unknown.
  // Used for the unknown-selector paths of mux_op/hotmux_op.
  static spool_ptr<Dlop> merge_unknown(const std::vector<const Dlop*>& cands);
  // Read the `extra` (unknown) bit at position `pos`, sign-extending past the
  // stored width — the unknown-bit analogue of bit_test.
  bool                   unknown_bit_test(int pos) const;

  bool has_extra() const {
    if (xkind == XKind::Zero) {
      return false;
    }
    if (xkind == XKind::Mirror) {
      return true;  // Mirror keeps data != 0
    }
    const int64_t* ep = bp + size;
    for (int i = 0; i < size; ++i) {
      if (ep[i] != 0) {
        return true;
      }
    }
    return false;
  }

public:
  Dlop() noexcept : type(Type::Invalid), xkind(XKind::Zero), size(0), shared_count(0), data(0) {}

  ~Dlop() { free_storage(); }

  // Embedded Dlops can be moved cheaply; the source is left empty so its
  // destructor is a no-op.
  Dlop(Dlop&& o) noexcept : type(o.type), xkind(o.xkind), size(o.size), shared_count(0) {
    if (o.on_heap()) {
      bp = o.bp;
    } else {
      data = o.data;
    }
    o.size  = 0;
    o.type  = Type::Invalid;
    o.xkind = XKind::Zero;
  }

  Dlop& operator=(Dlop&& o) noexcept {
    if (this != &o) {
      free_storage();
      type  = o.type;
      xkind = o.xkind;
      size  = o.size;
      if (o.on_heap()) {
        bp = o.bp;
      } else {
        data = o.data;
      }
      o.size  = 0;
      o.type  = Type::Invalid;
      o.xkind = XKind::Zero;
    }
    return *this;
  }

  // Deep copy: each Dlop owns its own word buffers. spool_ptr<Dlop> handles
  // ref-counted sharing separately.
  Dlop(const Dlop& o) : type(o.type), xkind(o.xkind), size(o.size), shared_count(0) {
    if (o.on_heap()) {
      bp = alloc(2 * size);
      memcpy(bp, o.bp, 2 * size * sizeof(int64_t));
    } else {
      data = o.data;
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
  // A value arithmetic / bitwise / shift ops can meaningfully operate on: a
  // (possibly unknown) Integer or Boolean. String, Nil, Invalid (incl. ref) and
  // Bitwidth are not numeric — any such operand makes an op illegal, and the op
  // returns nil() rather than crashing or producing a garbage bit-pattern.
  // Numeric values always carry size >= 1, so the kernels' size>=1 / non-empty
  // preconditions hold once this guard passes.
  bool is_numeric() const { return type == Type::Integer || type == Type::Boolean; }

  // --- In-place initializers (no spool_ptr round-trip) ---
  // These build the value directly into `*this`. Use them when a Dlop is
  // embedded in another struct, or via the `create_*` / `from_*` wrappers
  // when a pool-managed spool_ptr<Dlop> is needed.
  void init_bool(bool val);
  void init_integer(int64_t val);
  void init_string(std::string_view txt);
  void init_from_binary(std::string_view txt, bool unsigned_result);
  void init_from_pyrope(std::string_view orig_txt);
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

  // Interned from_pyrope: a literal is parsed ONCE per distinct text and the
  // result is memoized (init_from_pyrope -> Blop::shln is per-digit and showed
  // up re-parsing the same constants repeatedly in large IR walks). Returns a
  // const reference into a thread-local std::unordered_map (so callers on one
  // thread can hold it; copy it to outlive the thread or cross threads). The
  // node-based map keeps the reference stable across later inserts. Throws like
  // from_pyrope on a malformed literal (the failure is NOT cached). The cached
  // Dlop owns its own words via the immortal raw_ptr_pool, so the cache is safe
  // at thread teardown.
  static const Dlop& from_pyrope_cached(std::string_view txt);
  static spool_ptr<Dlop> from_ref(std::string_view txt);
  static spool_ptr<Dlop> invalid();

  static spool_ptr<Dlop> unknown(int nbits);
  static spool_ptr<Dlop> unknown_positive(int nbits);
  static spool_ptr<Dlop> unknown_negative(int nbits);
  // Boolean with an unknown value: type=Boolean with every bit unknown so
  // the result can collapse to either -1 (true) or 0 (false) and stays
  // sign-extending across any width consumer.
  static spool_ptr<Dlop> unknown_bool();

  // Pyrope nil literal — distinct from invalid()
  static spool_ptr<Dlop> nil();

  // Mask helpers (statics): (1<<bits)-1, contiguous slice [l..h], and ~((1<<bits)-1)
  static spool_ptr<Dlop> get_mask_value(int bits);
  static spool_ptr<Dlop> get_mask_value(int h, int l);
  static spool_ptr<Dlop> get_neg_mask_value(int bits);

  // Persistence: stable binary roundtrip. Layout (little-endian word order):
  //   [1 B] type, [2 B] size, [size * 8 B] base words, [size * 8 B] extra words
  std::string            serialize() const;
  static spool_ptr<Dlop> unserialize(std::string_view v);

  uint64_t hash() const;

protected:
  // --- Integer-amount op kernels ---
  // Implementation helpers for the shift / sign-extend ops. The public API
  // takes a Dlop operand; these run once the amount is confirmed numeric.
  spool_ptr<Dlop> shl_op(int64_t amount) const;
  spool_ptr<Dlop> sra_op(int64_t amount) const;
  spool_ptr<Dlop> sext_op(int from_bit) const;

public:
  // --- Arithmetic operations ---
  // Every binary op accepts (const Dlop&) or (spool_ptr<Dlop>); the spool
  // overload is a one-liner that delegates to the reference form, so embedded
  // Dlops and pool-managed spool_ptr<Dlop>s can mix freely without copies.
  spool_ptr<Dlop> add_op(const Dlop& other) const;
  spool_ptr<Dlop> add_op(spool_ptr<Dlop> other) const { return add_op(*other); }
  spool_ptr<Dlop> sub_op(const Dlop& other) const;
  spool_ptr<Dlop> sub_op(spool_ptr<Dlop> other) const { return sub_op(*other); }
  spool_ptr<Dlop> mult_op(const Dlop& other) const;
  spool_ptr<Dlop> mult_op(spool_ptr<Dlop> other) const { return mult_op(*other); }
  spool_ptr<Dlop> div_op(const Dlop& other) const;
  spool_ptr<Dlop> div_op(spool_ptr<Dlop> other) const { return div_op(*other); }
  // mod_op: integer modulo. Returns invalid for mod-by-zero (undefined),
  // a 1-bit unknown when either operand has unknowns, and the integer
  // remainder otherwise.
  spool_ptr<Dlop> mod_op(const Dlop& other) const;
  spool_ptr<Dlop> mod_op(spool_ptr<Dlop> other) const { return mod_op(*other); }
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
  // The shift amount is a Dlop operand (LiveHD passes shift counts as constant
  // nodes), resolved to a numeric value internally. Unknown shift amount →
  // 1-bit unknown result (the bit pattern is unrecoverable). Invalid / nil /
  // string amount → invalid.
  spool_ptr<Dlop> shl_op(const Dlop& amount) const;
  spool_ptr<Dlop> shl_op(spool_ptr<Dlop> amount) const { return shl_op(*amount); }
  spool_ptr<Dlop> sra_op(const Dlop& amount) const;
  spool_ptr<Dlop> sra_op(spool_ptr<Dlop> amount) const { return sra_op(*amount); }

  // --- Comparison operations ---
  spool_ptr<Dlop> eq_op(const Dlop& other) const;
  spool_ptr<Dlop> eq_op(spool_ptr<Dlop> other) const { return eq_op(*other); }
  // same_repr: structural compare of base AND extra. Two values with the same
  // unknown pattern (e.g. 0sb?1 vs 0sb?1) are equal. Use for containers, dedup,
  // and hashing where reflexivity is required.
  bool            same_repr(const Dlop& other) const;
  // is_known_eq: three-valued equality collapsed to bool — true only when both
  // sides are fully known and numerically equal; false if either side has any
  // unknown bits. Use in asserts where "definitely equal" is the question.
  bool            is_known_eq(const Dlop& other) const;

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
  // popcount_op: count of set bits as an Integer Dlop. With unknowns, the
  // exact count is not knowable: it lies in [ones, ones+u] where `ones` is the
  // number of known-set bits and `u` the number of unknown bits. The result is
  // the tightest ternary value (base/extra cube) that covers that whole range
  // — e.g. 0sb011?000 → 0ub1? (popcount 2 or 3), 0sb00111??00 → 0ub??? (3..5).
  // A negative value, or one with an unknown sign bit (0sb?...), has unbounded
  // popcount and returns a 1-bit unknown (0sb?). Distinct from the private
  // popcount() helper, which returns a plain int and ignores unknowns.
  spool_ptr<Dlop> popcount_op() const;

  // --- Bit manipulation ---
  // Sign-extend from the bit position given by the `bits` Dlop operand (a
  // numeric constant node). Non-numeric / unknown bit count → invalid.
  spool_ptr<Dlop> sext_op(const Dlop& bits) const;
  spool_ptr<Dlop> sext_op(spool_ptr<Dlop> bits) const { return sext_op(*bits); }
  spool_ptr<Dlop> get_mask_op() const;
  spool_ptr<Dlop> get_mask_op(const Dlop& mask) const;
  spool_ptr<Dlop> get_mask_op(spool_ptr<Dlop> mask) const { return get_mask_op(*mask); }
  spool_ptr<Dlop> set_mask_op(const Dlop& mask, const Dlop& value) const;
  spool_ptr<Dlop> set_mask_op(spool_ptr<Dlop> mask, spool_ptr<Dlop> value) const { return set_mask_op(*mask, *value); }
  spool_ptr<Dlop> concat_op(const Dlop& other) const;
  spool_ptr<Dlop> concat_op(spool_ptr<Dlop> other) const { return concat_op(*other); }
  spool_ptr<Dlop> adjust_bits(int amount) const;

  // --- Multi-input computing cells from livehd graph/cell.* ---
  // sum_op: all values on `a` are added and all values on `b` are
  // subtracted. This mirrors the LGraph Sum cell pin polarity.
  static spool_ptr<Dlop> sum_op(std::span<const spool_ptr<Dlop>> a, std::span<const spool_ptr<Dlop>> b);
  static spool_ptr<Dlop> sum_op(std::initializer_list<spool_ptr<Dlop>> a, std::initializer_list<spool_ptr<Dlop>> b) {
    return sum_op(std::span<const spool_ptr<Dlop>>(a.begin(), a.size()), std::span<const spool_ptr<Dlop>>(b.begin(), b.size()));
  }

  // --- Multiplexers / LUT (computing cells from livehd graph/cell.*) ---
  // These take the selector/address and the value list explicitly (static),
  // matching the LGraph node shape where pid 0 is the selector and pid 1..N
  // are the ordered values.
  //
  // mux_op: Y = values[sel] (0-based; sel == 0 picks values[0]). A fully known
  // selector picks one value exactly; a known out-of-range / non-integer
  // selector returns invalid(). When `sel` has unknown bits, every value whose
  // index is still reachable under the known/unknown bit pattern is ternary-
  // merged: bit positions that are known and equal across all reachable values
  // stay known, any differing or unknown bit becomes unknown.
  static spool_ptr<Dlop> mux_op(const Dlop& sel, std::span<const spool_ptr<Dlop>> values);
  static spool_ptr<Dlop> mux_op(const Dlop& sel, std::initializer_list<spool_ptr<Dlop>> values) {
    return mux_op(sel, std::span<const spool_ptr<Dlop>>(values.begin(), values.size()));
  }
  // hotmux_op: one-hot selector — bit `i` set selects values[i]. The selector
  // is asserted to be one-hot (at most one *known*-set bit). If exactly one
  // bit is known-set, that value is picked (one-hot guarantees the rest are 0,
  // even when they are unknown). If no bit is known-set but some bits are
  // unknown, the hot bit lies among the unknown positions and those values are
  // ternary-merged as in mux_op. A known all-zero selector returns invalid().
  static spool_ptr<Dlop> hotmux_op(const Dlop& sel, std::span<const spool_ptr<Dlop>> values);
  static spool_ptr<Dlop> hotmux_op(const Dlop& sel, std::initializer_list<spool_ptr<Dlop>> values) {
    return hotmux_op(sel, std::span<const spool_ptr<Dlop>>(values.begin(), values.size()));
  }
  // lut_op: Yosys `$lut` semantics — `table` is the 2^W-bit truth table and
  // `addr` is the index; the 1-bit result is `table[addr]` (bit `addr` of the
  // table, addr's LSB = first input). A known addr selects the bit directly
  // (sign-extended past the table width like any value). An unknown addr folds
  // every reachable table bit: all-equal-and-known → that bool, else 0sb? (a
  // 1-bit unknown).
  static spool_ptr<Dlop> lut_op(const Dlop& table, const Dlop& addr);

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
  int             get_bits() const;
  bool            bit_test(int pos) const;
  int             get_first_bit_set() const;
  int             get_last_bit_set() const;
  int             popcount() const;
  int             get_trailing_zeroes() const;
  bool            is_just_i64() const;
  int64_t         to_just_i64() const;

  // --- Conversion ---
  std::string to_pyrope() const;
  std::string to_binary() const;
  std::string to_verilog() const;
  std::string to_string() const;
  // Arbitrary-precision numeric renderers (no int64 round-trip — callers pick
  // the format). to_decimal_string is signed decimal ("-123"); to_hex_string is
  // "0x.."/"-0x..". Non-plain-integer values (string/unknown/invalid) fall back
  // to to_pyrope.
  std::string to_decimal_string() const;
  std::string to_hex_string() const;

  // Slop-PARITY formatting API (identical signatures/semantics to
  // Slop::to_decimal/to_hex/to_binary): `digits` zero-pads after any leading
  // '-', `sep` groups the digits '_'-separated every 4 (3 for decimal) from
  // the LSB. The shared entry points for ANY string interpolation — comptime
  // (upass constprop's __fmt fold over Dlop) and runtime (the sim driver over
  // Slop) — one algorithm, two value classes. Unknown bits render as '?' in
  // to_binary (inherited from to_binary() above).
  std::string        to_decimal(int digits = 0, bool sep = false) const;
  std::string        to_hex(int digits = 0, bool sep = false, bool upper = false) const;
  std::string        to_binary(int digits, bool sep) const;  // overload; to_binary() above stays
  static std::string pad_digits(std::string s, int digits);
  static std::string group_digits(std::string s, int group);

  void dump() const;
};

// Lock the layout win: type+xkind+size+shared_count (8) + union (8) = 16 bytes,
// down from the previous 24 (inline base/extra pair) and the original ~40.
// The common cases — known values up to 64 bits and the fully-unknown family
// (0sb?) — fit entirely inline with zero heap traffic.
static_assert(sizeof(Dlop) == 16, "Dlop layout regressed; expected 16-byte object");

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
