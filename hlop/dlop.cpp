//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "dlop.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <format>
#include <print>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>

#include "likely.hpp"

// =========================================================================
// Memory management
// =========================================================================
void Dlop::free(size_t sz, int64_t* ptr) {
  // Grow the per-thread pool on demand. `free` can be called for a size whose
  // pool slot was never populated on this thread — e.g. during late teardown
  // of long-lived bundles whose Dlops were allocated through a different
  // execution path. The growth mirrors `alloc` so released buffers always
  // have a home to return to.
  auto& pool = free_pool();
  while ((sz >> 3) >= pool.size()) {
    auto* p = new raw_ptr_pool((pool.size() + 1) << 6);
    pool.emplace_back(p);
  }
  pool[sz >> 3]->release_ptr(ptr);
}

int64_t* Dlop::alloc(size_t sz) {
  assert(sz >= 1);
  auto& pool = free_pool();
  if (likely(pool.size() > (sz >> 3))) {
    return static_cast<int64_t*>(pool[sz >> 3]->get_ptr());
  }
  while ((sz >> 3) >= pool.size()) {
    auto* ptr = new raw_ptr_pool((pool.size() + 1) << 6);
    pool.emplace_back(ptr);
  }
  return static_cast<int64_t*>(pool[sz >> 3]->get_ptr());
}

spool_ptr<Dlop> Dlop::make_result(Type tp, int16_t sz) {
  auto dlop = spool_ptr<Dlop>::make(tp, sz);
  return dlop;
}

void Dlop::grow_to(int16_t new_size) {
  if (new_size <= size) {
    return;
  }

  // Read existing payload through accessors before mutating size/layout. When
  // size == 0 there is no payload — treat the value as 0 (non-negative).
  int64_t base_sign  = (size > 0 && base()[size - 1] < 0) ? -1 : 0;
  int64_t extra_sign = (size > 0 && extra()[size - 1] < 0) ? -1 : 0;

  if (new_size <= 1) {
    // Only reachable from size == 0; layout stays inline (and Zero: a size-0
    // value has no payload, so both planes extend with 0).
    assert(!on_heap());
    data = base_sign;
    size = new_size;
    return;
  }

  int64_t* new_bp    = alloc(2 * new_size);
  int64_t* new_extra = new_bp + new_size;

  for (int i = 0; i < size; ++i) {
    new_bp[i]    = base()[i];
    new_extra[i] = extra()[i];
  }
  for (int i = size; i < new_size; ++i) {
    new_bp[i]    = base_sign;
    new_extra[i] = extra_sign;
  }

  free_storage();
  bp    = new_bp;
  xkind = XKind::Heap;
  size  = new_size;
}

void Dlop::normalize() {
  if (!on_heap()) {
    return;  // inline storage is already minimal
  }
  if (size == 1) {
    compact_inline();
    return;
  }

  const int64_t* bw = bp;
  const int64_t* ew = bp + size;

  int     min_size   = 1;
  int64_t base_sign  = bw[size - 1] < 0 ? -1 : 0;
  int64_t extra_sign = ew[size - 1] < 0 ? -1 : 0;

  for (int i = size - 1; i >= 1; --i) {
    if (bw[i] != base_sign || ew[i] != extra_sign) {
      min_size = i + 1;
      break;
    }
    // Check if removing this word would change the sign of the one below
    if ((i > 0) && ((bw[i - 1] < 0) != (base_sign < 0))) {
      min_size = i + 1;
      break;
    }
  }

  if (min_size >= size) {
    return;
  }

  if (min_size == 1) {
    // Switching from pool-backed storage to inline — read the surviving words
    // out before releasing the buffer that holds them.
    int64_t  b      = bw[0];
    int64_t  e      = ew[0];
    int64_t* old_bp = bp;
    int16_t  old_sz = size;
    size            = 1;
    data            = b;
    xkind           = XKind::Zero;
    free(2 * old_sz, old_bp);
    set_extra_word(e);  // Zero / Mirror / back to a 2-word heap buffer if mixed
  }
  // For simplicity, don't reallocate for intermediate sizes
}

// =========================================================================
// In-place initializers — fill `*this` directly. Used by create_* / from_*
// wrappers below, and by callers that embed Dlop in another struct.
// =========================================================================
void Dlop::init_bool(bool val) {
  reconstruct(Type::Boolean, 1);
  data = val ? -1 : 0;
}

void Dlop::init_integer(int64_t val) {
  reconstruct(Type::Integer, 1);
  data = val;
}

void Dlop::init_string(std::string_view txt) {
  reconstruct(Type::String, 1 + txt.size() / 8);
  for (int i = static_cast<int>(txt.size()) - 1; i >= 0; --i) {
    shl_base(8);
    or_base(static_cast<unsigned char>(txt[i]));
  }
}

void Dlop::init_from_ref(std::string_view txt) {
  if (txt.empty()) {
    init_invalid();
    return;
  }
  reconstruct(Type::Invalid, 1 + txt.size() / 8);
  for (int i = static_cast<int>(txt.size()) - 1; i >= 0; --i) {
    shl_base(8);
    or_base(static_cast<unsigned char>(txt[i]));
  }
}

void Dlop::init_invalid() { reconstruct(Type::Invalid, 0); }

void Dlop::init_nil() { reconstruct(Type::Nil, 0); }

void Dlop::init_unknown(int nbits) {
  init_integer(0);
  if (nbits <= 0) {
    return;
  }

  if (nbits <= 63) {
    int64_t mask = static_cast<int64_t>((uint64_t(1) << nbits) - 1);
    data         = mask;
    set_extra_word(mask);  // extra == base → Mirror, stays inline
  } else {
    int leftover = nbits % 64;
    int full     = nbits / 64;  // number of fully-unknown low words
    // ceil(nbits/64) unknown words, plus one all-zero headroom word when nbits
    // lands exactly on a 64-bit boundary, so bit `nbits` and above (including the
    // sign bit) stay known 0 instead of forming an unbounded negative unknown.
    // Matches get_mask_op()'s "words = nbits/64 + 1" headroom convention.
    int words = full + 1;
    grow_to(static_cast<int16_t>(words));
    int64_t* e = extra_mut();
    int64_t* b = base();
    for (int i = 0; i < words; ++i) {
      bool unk = i < full;
      b[i]     = unk ? -1 : 0;
      e[i]     = unk ? -1 : 0;
    }
    if (leftover > 0) {
      int64_t mask = static_cast<int64_t>((uint64_t(1) << leftover) - 1);
      b[full]      = mask;
      e[full]      = mask;
    }
    // leftover == 0: top word (index `full`) stays 0 as known-zero headroom.
  }
}

void Dlop::init_unknown_positive(int nbits) {
  if (nbits <= 1) {
    init_integer(0);
    return;
  }
  init_unknown(nbits - 1);
}

void Dlop::init_unknown_negative(int nbits) {
  if (nbits <= 1) {
    init_integer(-1);
    return;
  }
  init_unknown(nbits);
  int word = (nbits - 1) / 64;
  int bit  = (nbits - 1) % 64;
  if (on_heap()) {
    bp[size + word] &= ~(int64_t(1) << bit);
  } else {
    // nbits <= 63 → word 0; clearing the top unknown bit makes it mixed.
    set_extra_word(extra()[0] & ~(int64_t(1) << bit));
  }
}

// =========================================================================
// Factory methods — wrap an in-place initializer into a spool_ptr<Dlop>.
// =========================================================================
spool_ptr<Dlop> Dlop::create_bool(bool val) {
  auto dlop = spool_ptr<Dlop>::make();
  dlop->init_bool(val);
  return dlop;
}

spool_ptr<Dlop> Dlop::create_integer(int64_t val) {
  auto dlop = spool_ptr<Dlop>::make();
  dlop->init_integer(val);
  return dlop;
}

spool_ptr<Dlop> Dlop::create_string(std::string_view txt) {
  auto dlop = spool_ptr<Dlop>::make();
  dlop->init_string(txt);
  return dlop;
}

spool_ptr<Dlop> Dlop::from_string(std::string_view txt) { return create_string(txt); }

spool_ptr<Dlop> Dlop::from_ref(std::string_view txt) {
  auto dlop = spool_ptr<Dlop>::make();
  dlop->init_from_ref(txt);
  return dlop;
}

spool_ptr<Dlop> Dlop::invalid() {
  auto dlop = spool_ptr<Dlop>::make();
  dlop->init_invalid();
  return dlop;
}

spool_ptr<Dlop> Dlop::unknown(int nbits) {
  auto dlop = spool_ptr<Dlop>::make();
  dlop->init_unknown(nbits);
  return dlop;
}

spool_ptr<Dlop> Dlop::unknown_positive(int nbits) {
  auto dlop = spool_ptr<Dlop>::make();
  dlop->init_unknown_positive(nbits);
  return dlop;
}

spool_ptr<Dlop> Dlop::unknown_negative(int nbits) {
  auto dlop = spool_ptr<Dlop>::make();
  dlop->init_unknown_negative(nbits);
  return dlop;
}

spool_ptr<Dlop> Dlop::unknown_bool() {
  // Boolean unknown: type=Boolean, base=-1, extra=-1. Either resolves to
  // create_bool(true) (-1 across all bits) or create_bool(false) (0 across
  // all bits); the full-width unknown mask keeps the value sign-extending
  // for any consumer that walks bit positions beyond word 0.
  auto dlop = spool_ptr<Dlop>::make(Type::Boolean, 1);
  dlop->set_word_pair(-1, -1);  // extra == base → Mirror, stays inline
  return dlop;
}

// =========================================================================
// from_binary
// =========================================================================
void Dlop::init_from_binary(std::string_view txt, bool unsigned_result) {
  reconstruct(Type::Integer, 1 + txt.size() / 64);
  if (!unsigned_result) {
    for (size_t i = 0; i < txt.size(); ++i) {
      const auto ch2 = txt[i];
      if (ch2 == '_') {
        continue;
      }
      if (ch2 == '1') {
        extend_base(-1);
      } else if (ch2 == '?') {
        // Unknown sign: extend both base and extra so the sign-extended
        // bits maintain the invariant (unknown bits have base = 1).
        extend_base(-1);
        extend_extra(-1);
      }
      break;
    }
  }

  for (size_t i = 0; i < txt.size(); ++i) {
    const auto ch2 = txt[i];
    if (ch2 == '_') {
      continue;
    }

    shl_base(1);
    shl_extra(1);
    if (ch2 == '?' || ch2 == 'x') {
      or_extra(1);
      or_base(1);  // unknown bits have base=1 (invariant: base == base|extra)
    } else if (ch2 == 'z') {
      or_extra(1);
      or_base(1);
    } else if (ch2 == '0') {
      // nothing
    } else if (ch2 == '1') {
      or_base(1);
    } else {
      throw std::runtime_error(std::format("ERROR: {} binary encoding could not use {}\n", txt, ch2));
    }
  }

  // The bit-building loop above goes through the heap once unknowns appear;
  // reclaim the inline Mirror/Zero encodings when the final pattern allows
  // (e.g. a plain "?" ends as base == extra == -1).
  compact_inline();
}

spool_ptr<Dlop> Dlop::from_binary(std::string_view txt, bool unsigned_result) {
  auto dlop = spool_ptr<Dlop>::make();
  dlop->init_from_binary(txt, unsigned_result);
  return dlop;
}

// =========================================================================
// from_pyrope
// =========================================================================
void Dlop::init_from_pyrope(std::string_view orig_txt) {
  if (orig_txt.empty()) {
    init_invalid();
    return;
  }

  // Operate directly on the input string_view — avoids the std::string copy
  // that `str_tools::to_lower` did. Per-character lowercasing is local; the
  // hex char_to_val table already accepts both cases for `[a-fA-F]`.
  auto lower = [](char c) -> char { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
  auto eq_ci = [&](std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
      return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
      if (lower(a[i]) != b[i]) {
        return false;
      }
    }
    return true;
  };

  if (eq_ci(orig_txt, "true")) {
    init_bool(true);
    return;
  }
  if (eq_ci(orig_txt, "false")) {
    init_bool(false);
    return;
  }
  // Pyrope `nil` / `null` literals (case-insensitive) parse to Type::Nil.
  // To represent the *string* "nil", use the quoted form `'nil'`, which
  // takes the quoted-string branch below.
  if (eq_ci(orig_txt, "nil") || eq_ci(orig_txt, "null")) {
    init_nil();
    return;
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

  if (orig_txt.size() >= (1 + skip_chars) && std::isdigit(static_cast<unsigned char>(orig_txt[skip_chars]))) {
    shift_mode = 10;
    if (orig_txt.size() >= (2 + skip_chars) && orig_txt[skip_chars] == '0') {
      ++skip_chars;
      char sel_ch = lower(orig_txt[skip_chars]);
      if (sel_ch == 's') {
        // Signed literals are binary only: the prefix must be the full `0sb…`.
        // Bounds-check before reading the base char so a bare `0s` does not read
        // past the string_view.
        ++skip_chars;
        if (skip_chars >= orig_txt.size() || lower(orig_txt[skip_chars]) != 'b') {
          throw std::runtime_error(std::format("ERROR: {} unknown pyrope encoding only binary can be signed 0sb...\n", orig_txt));
        }
        sel_ch = 'b';
        assert(!unsigned_result);
      } else if (sel_ch == 'u') {
        // Explicit `0u` prefix: unsigned, then a base selector (x/b/d/o)
        // follows — the binary form is `0ub…`. Bounds-check so a bare `0u` does
        // not read past the string_view.
        ++skip_chars;
        if (skip_chars >= orig_txt.size()) {
          throw std::runtime_error(std::format("ERROR: {} unknown pyrope encoding, use 0ub... (binary) or 0ux/0ud/0uo\n", orig_txt));
        }
        sel_ch          = lower(orig_txt[skip_chars]);
        unsigned_result = true;
      } else {
        // No explicit sign. Binary literals MUST be explicit about their
        // signedness — `0b…` is no longer accepted; use `0ub…` (unsigned) or
        // `0sb…` (signed). Other bases (hex/decimal/octal) stay unsigned by
        // default.
        if (sel_ch == 'b') {
          throw std::runtime_error(
              std::format("ERROR: {} binary literal needs an explicit sign: use 0ub… (unsigned) or 0sb… (signed)\n", orig_txt));
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
      } else if (std::isdigit(static_cast<unsigned char>(sel_ch))) {
        shift_mode = 10;
      } else if (sel_ch == 'o') {
        shift_mode = 3;
        ++skip_chars;
      } else {
        throw std::runtime_error(std::format("ERROR: {} unknown pyrope encoding (leading {})\n", orig_txt, sel_ch));
      }
    }
  } else {
    size_t start_i = orig_txt.size();
    size_t end_i   = 0;

    if (orig_txt.size() > 1 && orig_txt.front() == '\'' && orig_txt.back() == '\'') {
      --start_i;
      ++end_i;
    }

    init_string(orig_txt.substr(end_i, start_i - end_i));
    return;
  }

  // Power-of-two decimal modifiers (02-basics.md): a single trailing K/M/G/T
  // scales a decimal literal by 1024^n (K=2^10, M=2^20, G=2^30, T=2^40). Strip
  // it before the digit loop so the suffix is not scanned as a digit. Only the
  // decimal branch (shift_mode==10) is affected; hex/octal/binary are untouched.
  int    scale_pow2 = 0;
  size_t dec_end    = orig_txt.size();
  if (shift_mode == 10 && dec_end > skip_chars + 1) {
    switch (lower(orig_txt[dec_end - 1])) {
      case 'k': scale_pow2 = 10; break;
      case 'm': scale_pow2 = 20; break;
      case 'g': scale_pow2 = 30; break;
      case 't': scale_pow2 = 40; break;
      default: break;
    }
    if (scale_pow2 != 0) {
      --dec_end;  // drop the suffix from the magnitude scan
    }
  }

  reconstruct(Type::Integer, 1 + orig_txt.size() / 16 + (scale_pow2 != 0 ? 1 : 0));

  if (shift_mode == 10) {
    for (size_t i = skip_chars; i < dec_end; ++i) {
      char c = orig_txt[i];
      auto v = char_to_val[static_cast<uint8_t>(c)];
      if (likely(v >= 0 && v < 10)) {
        mult_base(10);
        add_base(v);
      } else {
        if (c == '_') {
          continue;
        }
        throw std::runtime_error(std::format("ERROR: {} encoding could not use {}\n", orig_txt, c));
      }
    }
    if (scale_pow2 != 0) {
      shl_base(scale_pow2);  // magnitude * 1024^n
    }
  } else if (shift_mode == 1) {
    init_from_binary(orig_txt.substr(skip_chars), unsigned_result);
    if (negative) {
      negate_mut();
    }
    return;
  } else {
    assert(shift_mode == 3 || shift_mode == 4);

    for (size_t i = skip_chars; i < orig_txt.size(); ++i) {
      char c = orig_txt[i];
      if (c == '_') {
        continue;
      }

      auto v = char_to_val[static_cast<uint8_t>(c)];
      if (unlikely(v < 0)) {
        throw std::runtime_error(std::format("ERROR: {} encoding could not use {}\n", orig_txt, c));
      }

      auto char_sa = char_to_bits[static_cast<uint8_t>(c)];
      if (unlikely(char_sa > shift_mode)) {
        throw std::runtime_error(std::format("ERROR: {} invalid syntax for number {} bits needed for '{}'", orig_txt, char_sa, c));
      }
      shl_base(shift_mode);
      or_base(v);
    }

    assert(unsigned_result);
  }

  if (negative) {
    negate_mut();
  }
}

spool_ptr<Dlop> Dlop::from_pyrope(std::string_view orig_txt) {
  auto dlop = spool_ptr<Dlop>::make();
  dlop->init_from_pyrope(orig_txt);
  return dlop;
}

const Dlop& Dlop::from_pyrope_cached(std::string_view txt) {
  // Thread-local so it shares the calling thread's raw_ptr_pool and needs no
  // lock. std::unordered_map keeps references stable across inserts (node-based);
  // a transparent hash/eq lets us look up by string_view without allocating a key
  // on a hit. The immortal raw_ptr_pool keeps the cached Dlops' word buffers
  // valid through thread teardown, so the cache is safe to destroy at thread end.
  struct Sv_hash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
  };
  struct Sv_eq {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
  };
  static thread_local std::unordered_map<std::string, Dlop, Sv_hash, Sv_eq> cache;
  if (auto it = cache.find(txt); it != cache.end()) {
    return it->second;
  }
  Dlop parsed;
  parsed.init_from_pyrope(txt);  // throws on a malformed literal; not cached (matches from_pyrope)
  return cache.emplace(std::string(txt), std::move(parsed)).first->second;
}

// =========================================================================
// Arithmetic operations
// =========================================================================
// Carry-chain-conservative unknown propagation: any unknown bit at position
// `i` in either operand can affect every bit at or above `i` in the sum (via
// the ripple carry). So the result's extra mask sets every bit at or above
// the lowest unknown bit across both operands. Above that position we also
// force base bits to 1 to maintain the invariant `base == base | extra`.
spool_ptr<Dlop> Dlop::add_op(const Dlop& other) const {
  // Illegal operand (string / nil / invalid / ref) → nil, never a crash.
  if (!is_numeric() || !other.is_numeric()) {
    return nil();
  }
  // Fully-known size-1 fast path. The exact sum of two ~64-bit same-sign values
  // carries out into a second word; detect that and widen so the result stays
  // exact (Dlop is arbitrary precision — addition is never modular).
  if (size == 1 && other.size == 1 && !has_extra() && !other.has_extra()) {
    __int128 sum = static_cast<__int128>(base()[0]) + static_cast<__int128>(other.base()[0]);
    int64_t  lo  = static_cast<int64_t>(static_cast<uint64_t>(sum));
    int64_t  hi  = static_cast<int64_t>(sum >> 64);
    if (hi == (lo < 0 ? -1 : 0)) {  // fits in one signed word
      auto dlop = make_result(Type::Integer, 1);
      dlop->set_word_pair(lo, 0);
      return dlop;
    }
    auto dlop       = make_result(Type::Integer, 2);
    dlop->base()[0] = lo;
    dlop->base()[1] = hi;
    dlop->zero_extra();
    return dlop;
  }

  // Size-1 with unknowns: bits at or above the lowest unknown become unknown,
  // which already absorbs any carry-out, so the result stays one word.
  if (size == 1 && other.size == 1) {
    auto     dlop     = make_result(Type::Integer, 1);
    uint64_t combined = static_cast<uint64_t>(extra()[0]) | static_cast<uint64_t>(other.extra()[0]);
    // hi_fill: all bits at or above the lowest set bit of combined.
    uint64_t hi_fill  = 0u - (combined & (0u - combined));
    int64_t  e        = static_cast<int64_t>(hi_fill);
    int64_t  b        = (base()[0] + other.base()[0]) | e;  // maintain invariant
    dlop->set_word_pair(b, e);
    return dlop;
  }

  // General path: one extra word of carry headroom, then normalize to minimal.
  int16_t rsz  = static_cast<int16_t>(std::max(size, other.size) + 1);
  auto    dlop = make_result(Type::Integer, rsz);

  {
    // Multi-word: sign-extend shorter operand
    const int64_t* s1 = base();
    const int64_t* s2 = other.base();
    const int64_t* e1 = extra();
    const int64_t* e2 = other.extra();

    // Use temp arrays for sign extension if needed
    int64_t* s1_ext = nullptr;
    int64_t* s2_ext = nullptr;
    int64_t* e1_ext = nullptr;
    int64_t* e2_ext = nullptr;

    if (size < rsz) {
      s1_ext = alloc(rsz);
      e1_ext = alloc(rsz);
      Blop::extend(s1_ext, rsz, base()[size - 1] < 0 ? -1 : 0);
      Blop::extend(e1_ext, rsz, extra()[size - 1] < 0 ? -1 : 0);
      for (int i = 0; i < size; ++i) {
        s1_ext[i] = base()[i];
        e1_ext[i] = extra()[i];
      }
      s1 = s1_ext;
      e1 = e1_ext;
    }
    if (other.size < rsz) {
      s2_ext = alloc(rsz);
      e2_ext = alloc(rsz);
      Blop::extend(s2_ext, rsz, other.base()[other.size - 1] < 0 ? -1 : 0);
      Blop::extend(e2_ext, rsz, other.extra()[other.size - 1] < 0 ? -1 : 0);
      for (int i = 0; i < other.size; ++i) {
        s2_ext[i] = other.base()[i];
        e2_ext[i] = other.extra()[i];
      }
      s2 = s2_ext;
      e2 = e2_ext;
    }

    Blop::addn(dlop->base(), rsz, s1, s2);

    if (!has_extra() && !other.has_extra()) {
      dlop->zero_extra();
    } else {
      // Find the lowest unknown bit across all words of (e1 | e2).
      int lowest_word = -1;
      int lowest_bit  = 0;
      for (int w = 0; w < rsz; ++w) {
        uint64_t c = static_cast<uint64_t>(e1[w]) | static_cast<uint64_t>(e2[w]);
        if (c != 0) {
          lowest_word = w;
          lowest_bit  = __builtin_ctzll(c);
          break;
        }
      }
      if (lowest_word < 0) {
        dlop->zero_extra();
      } else {
        int64_t* re = dlop->extra_mut();
        int64_t* rb = dlop->base();
        for (int w = 0; w < rsz; ++w) {
          if (w < lowest_word) {
            re[w] = 0;
          } else if (w == lowest_word) {
            uint64_t lowmask = static_cast<uint64_t>(1) << lowest_bit;
            re[w]            = static_cast<int64_t>(0u - lowmask);
          } else {
            re[w] = -1;  // every bit above is unknown
          }
        }
        // Maintain invariant: unknown bits have base = 1.
        for (int w = 0; w < rsz; ++w) {
          rb[w] |= re[w];
        }
      }
    }

    if (s1_ext) {
      free(rsz, s1_ext);
      free(rsz, e1_ext);
    }
    if (s2_ext) {
      free(rsz, s2_ext);
      free(rsz, e2_ext);
    }
  }

  dlop->normalize();
  return dlop;
}

spool_ptr<Dlop> Dlop::sub_op(const Dlop& other) const {
  // Illegal operand (string / nil / invalid / ref) → nil. Guard here too so a
  // bad `other` never reaches neg_op()'s size-0 kernel path.
  if (!is_numeric() || !other.is_numeric()) {
    return nil();
  }
  // sub = add(neg(other))
  auto neg_other = other.neg_op();
  return add_op(neg_other);
}

spool_ptr<Dlop> Dlop::sum_op(std::span<const spool_ptr<Dlop>> a, std::span<const spool_ptr<Dlop>> b) {
  auto result = create_integer(0);
  for (const auto& v : a) {
    result = result->add_op(v);
  }
  for (const auto& v : b) {
    result = result->sub_op(v);
  }
  return result;
}

// Unknown propagation for multiply: bit k of (a*b) depends on every (i,j)
// with i+j ≤ k. The lowest output bit that can be tainted by an unknown is
// `min(lu_a, lu_b)`, the lowest unknown across both operands — for any k
// below that, every contributing a_i, b_j is known. From that bit up, mark
// unknown (the carry chain mirrors add).
spool_ptr<Dlop> Dlop::mult_op(const Dlop& other) const {
  // Illegal operand (string / nil / invalid / ref) → nil. Besides the semantic
  // requirement this also avoids Blop::multn reading src[-1] for a size-0
  // operand.
  if (!is_numeric() || !other.is_numeric()) {
    return nil();
  }

  // Fully-known size-1 fast path: the dominant small multiply. The general path
  // allocates an rsz=2 pool buffer + memsets that normalize() almost always
  // undoes (a ~31x31-bit product fits one word). Mirror add_op's fast path.
  if (size == 1 && other.size == 1 && !has_extra() && !other.has_extra()) {
    __int128 prod = static_cast<__int128>(base()[0]) * static_cast<__int128>(other.base()[0]);
    int64_t  lo   = static_cast<int64_t>(static_cast<uint64_t>(prod));
    int64_t  hi   = static_cast<int64_t>(prod >> 64);
    if (hi == (lo < 0 ? -1 : 0)) {  // fits in one signed word
      auto dlop = make_result(Type::Integer, 1);
      dlop->set_word_pair(lo, 0);
      return dlop;
    }
    auto dlop       = make_result(Type::Integer, 2);
    dlop->base()[0] = lo;
    dlop->base()[1] = hi;
    dlop->zero_extra();
    return dlop;
  }

  int16_t rsz  = size + other.size;
  auto    dlop = make_result(Type::Integer, rsz);

  // Compute the multiplication on base() unconditionally. For known low bits
  // this is correct; for unknown-tainted upper bits we overwrite extra/base
  // below.
  if (size == 1 && other.size == 1) {
    __int128 prod   = static_cast<__int128>(base()[0]) * other.base()[0];
    dlop->base()[0] = static_cast<int64_t>(prod);
    if (rsz > 1) {
      dlop->base()[1] = static_cast<int64_t>(prod >> 64);
    }
  } else {
    Blop::multn(dlop->base(), rsz, base(), size, other.base(), other.size);
  }

  if (!has_unknowns() && !other.has_unknowns()) {
    dlop->zero_extra();
    dlop->normalize();
    return dlop;
  }

  auto lowest_unk = [](const int64_t* e, int sz) -> int {
    for (int w = 0; w < sz; ++w) {
      if (e[w] != 0) {
        return w * 64 + __builtin_ctzll(static_cast<uint64_t>(e[w]));
      }
    }
    return -1;
  };
  int lu_a = lowest_unk(extra(), size);
  int lu_b = lowest_unk(other.extra(), other.size);
  int lu   = (lu_a < 0) ? lu_b : (lu_b < 0) ? lu_a : std::min(lu_a, lu_b);

  // lu < 0 is unreachable here (at least one operand has unknowns), but
  // guard anyway.
  if (lu < 0) {
    dlop->zero_extra();
  } else {
    int      lu_word = lu / 64;
    int      lu_bit  = lu % 64;
    int64_t* re      = dlop->extra_mut();
    int64_t* rb      = dlop->base();
    for (int w = 0; w < rsz; ++w) {
      if (w < lu_word) {
        re[w] = 0;
      } else if (w == lu_word) {
        uint64_t lowmask = static_cast<uint64_t>(1) << lu_bit;
        re[w]            = static_cast<int64_t>(0u - lowmask);
      } else {
        re[w] = -1;
      }
    }
    for (int w = 0; w < rsz; ++w) {
      rb[w] |= re[w];
    }
  }

  dlop->normalize();
  return dlop;
}

spool_ptr<Dlop> Dlop::div_op(const Dlop& other) const {
  // Illegal operand (string / nil / invalid / ref) → nil. Must precede the
  // is_known_false() check below: a size-0 nil/invalid reads as "false" and
  // would otherwise be mistaken for a zero divisor.
  if (!is_numeric() || !other.is_numeric()) {
    return nil();
  }
  if (other.is_known_false()) {
    return nil();  // division by zero → nil
  }

  if (has_unknowns() || other.has_unknowns()) {
    int b = get_bits();
    if (!other.has_unknowns()) {
      b -= other.get_bits();
      if (b <= 0) {
        return create_integer(0);
      }
    }
    bool neg1 = is_negative();
    bool neg2 = other.is_negative();
    if (neg1 != neg2) {
      return unknown_negative(b);
    }
    return unknown_positive(b);
  }

  // Division by ±1 is the one case whose quotient can overflow the dividend's
  // width: the most-negative value over -1 is -(min) == 2^(w-1), needing an
  // extra word, and INT64_MIN/-1 is signed-overflow UB in the scalar fast path
  // below. Route through neg_op()/copy, which already widen correctly.
  if (other.size == 1) {
    if (other.base()[0] == 1) {
      auto dlop = make_result(Type::Integer, size);
      dlop->copy_payload_from(*this);  // x / 1 == x
      dlop->zero_extra();
      dlop->normalize();
      return dlop;
    }
    if (other.base()[0] == -1) {
      return neg_op();  // x / -1 == -x  (neg_op widens the most-negative value)
    }
  }

  auto dlop = make_result(Type::Integer, size);

  if (size == 1 && other.size == 1) {
    assert(other.base()[0] != 0);
    dlop->set_word_pair(base()[0] / other.base()[0], 0);
  } else {
    Blop::divn(dlop->base(), size, base(), size, other.base(), other.size);
    dlop->zero_extra();
  }

  dlop->normalize();
  return dlop;
}

// mod_op: integer remainder, truncating toward zero (sign follows the
// dividend, matching C/C++ `%`). Returns invalid on mod-by-zero (undefined)
// and a 1-bit unknown when either operand has unknowns; otherwise the exact
// remainder at any width.
spool_ptr<Dlop> Dlop::mod_op(const Dlop& other) const {
  // Illegal operand (string / nil / invalid / ref) → nil. Must precede the
  // is_known_false() check: a size-0 nil/invalid reads as "false" and would
  // otherwise be misclassified as "mod by zero".
  if (!is_numeric() || !other.is_numeric()) {
    return nil();
  }
  if (has_unknowns() || other.has_unknowns()) {
    return unknown(1);
  }
  if (other.is_known_false()) {
    return nil();  // mod by zero → nil
  }
  // x % ±1 == 0. Handled explicitly so the scalar fast path never evaluates
  // INT64_MIN % -1 (signed-overflow UB).
  if (other.size == 1 && (other.base()[0] == 1 || other.base()[0] == -1)) {
    return create_integer(0);
  }
  if (size == 1 && other.size == 1) {
    return create_integer(base()[0] % other.base()[0]);
  }

  // The remainder magnitude is below the divisor, so max(size, other.size)
  // words always suffice.
  int16_t rsz  = size > other.size ? size : other.size;
  auto    dlop = make_result(Type::Integer, rsz);
  Blop::modn(dlop->base(), rsz, base(), size, other.base(), other.size);
  dlop->zero_extra();
  dlop->normalize();
  return dlop;
}

// neg = ~x + 1; the "+1" has a carry chain identical to add. So bits at or
// above the lowest unknown become unknown; bits below stay deterministic.
spool_ptr<Dlop> Dlop::neg_op() const {
  // Illegal operand (string / nil / invalid / ref) → nil. A numeric value
  // always has size >= 1, so the size-1 reads and Blop::negn(size>=1) below are
  // safe once this guard passes.
  if (!is_numeric()) {
    return nil();
  }
  auto dlop = make_result(Type::Integer, size);

  if (has_unknowns()) {
    if (size == 1) {
      uint64_t eu = static_cast<uint64_t>(extra()[0]);
      uint64_t hi = 0u - (eu & (0u - eu));
      int64_t  e  = static_cast<int64_t>(hi);
      int64_t  b  = (-base()[0]) | e;
      dlop->set_word_pair(b, e);
    } else {
      Blop::negn(dlop->base(), size, base());
      // Find lowest unknown across all words.
      int lu_word = -1;
      int lu_bit  = 0;
      for (int w = 0; w < size; ++w) {
        if (extra()[w] != 0) {
          lu_word = w;
          lu_bit  = __builtin_ctzll(static_cast<uint64_t>(extra()[w]));
          break;
        }
      }
      if (lu_word < 0) {
        dlop->zero_extra();
      } else {
        int64_t* re = dlop->extra_mut();
        int64_t* rb = dlop->base();
        for (int w = 0; w < size; ++w) {
          if (w < lu_word) {
            re[w] = 0;
          } else if (w == lu_word) {
            uint64_t lowmask = static_cast<uint64_t>(1) << lu_bit;
            re[w]            = static_cast<int64_t>(0u - lowmask);
          } else {
            re[w] = -1;
          }
        }
        for (int w = 0; w < size; ++w) {
          rb[w] |= re[w];
        }
      }
    }
    return dlop;
  }

  // Negating the most-negative value of a given width overflows it (|min| needs
  // one more bit: -(-2^(64k-1)) = 2^(64k-1)), so widen by a word in that case.
  bool most_neg = static_cast<uint64_t>(base()[size - 1]) == (static_cast<uint64_t>(1) << 63);
  for (int i = 0; most_neg && i < size - 1; ++i) {
    if (base()[i] != 0) {
      most_neg = false;
    }
  }
  if (most_neg) {
    auto     big = make_result(Type::Integer, static_cast<int16_t>(size + 1));
    int64_t* b   = big->base();
    for (int i = 0; i < size + 1; ++i) {
      b[i] = 0;
    }
    b[size - 1] = static_cast<int64_t>(static_cast<uint64_t>(1) << 63);  // 2^(64*size-1)
    big->zero_extra();
    big->normalize();
    return big;
  }

  if (size == 1) {
    dlop->set_word_pair(-base()[0], 0);
  } else {
    Blop::negn(dlop->base(), size, base());
    dlop->zero_extra();
  }

  return dlop;
}

// =========================================================================
// Bitwise operations
// =========================================================================
spool_ptr<Dlop> Dlop::or_op(const Dlop& other) const {
  // Nil propagates, with a boolean short-circuit: a known-true non-nil
  // operand folds the result to 1 (true OR anything = true), otherwise the
  // unset operand poisons the result back to nil. Mirrors `and_op`'s
  // symmetric handling and avoids the size=0 UB that the bitwise paths below
  // would hit when one side has Type::Nil.
  if (is_nil() || other.is_nil()) {
    const bool self_nil  = is_nil();
    const bool other_nil = other.is_nil();
    if ((!self_nil && is_known_true()) || (!other_nil && other.is_known_true())) {
      return create_integer(1);
    }
    return nil();
  }
  // Any other non-numeric operand (string / invalid / ref) is illegal → nil.
  if (!is_numeric() || !other.is_numeric()) {
    return nil();
  }

  int16_t rsz  = std::max(size, other.size);
  auto    dlop = make_result(Type::Integer, rsz);

  // For OR with unknowns (base/extra encoding where unknown bits have base=1):
  //   known_1 = base() & ~extra  (definitely 1)
  //   known_0 = ~base          (definitely 0, since unknown has base=1)
  //   A known 1 in either input makes result known 1
  //   Both known 0 makes result known 0
  //   Otherwise unknown

  if (rsz == 1 && size == 1 && other.size == 1) {
    if (extra()[0] == 0 && other.extra()[0] == 0) {
      dlop->set_word_pair(base()[0] | other.base()[0], 0);
    } else {
      int64_t known1_a      = base()[0] & ~extra()[0];
      int64_t known1_b      = other.base()[0] & ~other.extra()[0];
      int64_t known0_a      = ~base()[0];
      int64_t known0_b      = ~other.base()[0];
      int64_t result_known1 = known1_a | known1_b;
      int64_t result_known0 = known0_a & known0_b;
      int64_t e             = ~result_known1 & ~result_known0;
      dlop->set_word_pair(result_known1 | e, e);  // unknown bits have base=1
    }
  } else {
    // Sign-extend shorter operand
    const int64_t *s1 = base(), *s2 = other.base();
    const int64_t *e1 = extra(), *e2 = other.extra();
    int64_t *      s1_ext = nullptr, *s2_ext = nullptr, *e1_ext = nullptr, *e2_ext = nullptr;

    if (size < rsz) {
      s1_ext = alloc(rsz);
      e1_ext = alloc(rsz);
      Blop::extend(s1_ext, rsz, base()[size - 1] < 0 ? -1 : 0);
      Blop::extend(e1_ext, rsz, extra()[size - 1] < 0 ? -1 : 0);
      for (int i = 0; i < size; ++i) {
        s1_ext[i] = base()[i];
        e1_ext[i] = extra()[i];
      }
      s1 = s1_ext;
      e1 = e1_ext;
    }
    if (other.size < rsz) {
      s2_ext = alloc(rsz);
      e2_ext = alloc(rsz);
      Blop::extend(s2_ext, rsz, other.base()[other.size - 1] < 0 ? -1 : 0);
      Blop::extend(e2_ext, rsz, other.extra()[other.size - 1] < 0 ? -1 : 0);
      for (int i = 0; i < other.size; ++i) {
        s2_ext[i] = other.base()[i];
        e2_ext[i] = other.extra()[i];
      }
      s2 = s2_ext;
      e2 = e2_ext;
    }

    if (!has_extra() && !other.has_extra()) {
      Blop::orn(dlop->base(), rsz, s1, s2);
      dlop->zero_extra();
    } else {
      int64_t* re = dlop->extra_mut();
      int64_t* rb = dlop->base();
      for (int i = 0; i < rsz; ++i) {
        int64_t known1_a      = s1[i] & ~e1[i];
        int64_t known1_b      = s2[i] & ~e2[i];
        int64_t known0_a      = ~s1[i];
        int64_t known0_b      = ~s2[i];
        int64_t result_known1 = known1_a | known1_b;
        int64_t result_known0 = known0_a & known0_b;
        re[i]                 = ~result_known1 & ~result_known0;
        rb[i]                 = result_known1 | re[i];
      }
    }

    if (s1_ext) {
      free(rsz, s1_ext);
      free(rsz, e1_ext);
    }
    if (s2_ext) {
      free(rsz, s2_ext);
      free(rsz, e2_ext);
    }
  }

  return dlop;
}

spool_ptr<Dlop> Dlop::and_op(const Dlop& other) const {
  // Nil propagates, with a boolean short-circuit: a known-false non-nil
  // operand folds the result to 0 (false AND anything = false), otherwise
  // the unset operand poisons the result back to nil. Without this guard the
  // size=0 nil falls into the bitwise path below and yields a UB zero.
  if (is_nil() || other.is_nil()) {
    const bool self_nil  = is_nil();
    const bool other_nil = other.is_nil();
    if ((!self_nil && is_known_false()) || (!other_nil && other.is_known_false())) {
      return create_integer(0);
    }
    return nil();
  }
  // Any other non-numeric operand (string / invalid / ref) is illegal → nil.
  if (!is_numeric() || !other.is_numeric()) {
    return nil();
  }

  int16_t rsz  = std::max(size, other.size);
  auto    dlop = make_result(Type::Integer, rsz);

  // For AND with unknowns (base/extra encoding where unknown bits have base=1):
  //   known_0 = ~base          (definitely 0)
  //   known_1 = base() & ~extra  (definitely 1)
  //   A known 0 in either input makes result known 0
  //   Both known 1 makes result known 1
  //   Otherwise unknown

  if (rsz == 1 && size == 1 && other.size == 1) {
    if (extra()[0] == 0 && other.extra()[0] == 0) {
      dlop->set_word_pair(base()[0] & other.base()[0], 0);
    } else {
      int64_t known0_a      = ~base()[0];
      int64_t known0_b      = ~other.base()[0];
      int64_t known1_a      = base()[0] & ~extra()[0];
      int64_t known1_b      = other.base()[0] & ~other.extra()[0];
      int64_t result_known0 = known0_a | known0_b;
      int64_t result_known1 = known1_a & known1_b;
      int64_t e             = ~result_known0 & ~result_known1;
      dlop->set_word_pair(result_known1 | e, e);
    }
  } else {
    const int64_t *s1 = base(), *s2 = other.base();
    const int64_t *e1 = extra(), *e2 = other.extra();
    int64_t *      s1_ext = nullptr, *s2_ext = nullptr, *e1_ext = nullptr, *e2_ext = nullptr;

    if (size < rsz) {
      s1_ext = alloc(rsz);
      e1_ext = alloc(rsz);
      Blop::extend(s1_ext, rsz, base()[size - 1] < 0 ? -1 : 0);
      Blop::extend(e1_ext, rsz, extra()[size - 1] < 0 ? -1 : 0);
      for (int i = 0; i < size; ++i) {
        s1_ext[i] = base()[i];
        e1_ext[i] = extra()[i];
      }
      s1 = s1_ext;
      e1 = e1_ext;
    }
    if (other.size < rsz) {
      s2_ext = alloc(rsz);
      e2_ext = alloc(rsz);
      Blop::extend(s2_ext, rsz, other.base()[other.size - 1] < 0 ? -1 : 0);
      Blop::extend(e2_ext, rsz, other.extra()[other.size - 1] < 0 ? -1 : 0);
      for (int i = 0; i < other.size; ++i) {
        s2_ext[i] = other.base()[i];
        e2_ext[i] = other.extra()[i];
      }
      s2 = s2_ext;
      e2 = e2_ext;
    }

    if (!has_extra() && !other.has_extra()) {
      Blop::andn(dlop->base(), rsz, s1, s2);
      dlop->zero_extra();
    } else {
      int64_t* re = dlop->extra_mut();
      int64_t* rb = dlop->base();
      for (int i = 0; i < rsz; ++i) {
        int64_t known0_a      = ~s1[i];
        int64_t known0_b      = ~s2[i];
        int64_t known1_a      = s1[i] & ~e1[i];
        int64_t known1_b      = s2[i] & ~e2[i];
        int64_t result_known0 = known0_a | known0_b;
        int64_t result_known1 = known1_a & known1_b;
        re[i]                 = ~result_known0 & ~result_known1;
        rb[i]                 = result_known1 | re[i];
      }
    }

    if (s1_ext) {
      free(rsz, s1_ext);
      free(rsz, e1_ext);
    }
    if (s2_ext) {
      free(rsz, s2_ext);
      free(rsz, e2_ext);
    }
  }

  return dlop;
}

spool_ptr<Dlop> Dlop::xor_op(const Dlop& other) const {
  // Nil propagates. XOR has no short-circuit identity (no value of the
  // non-nil operand can resolve the result), so any nil operand yields nil.
  if (is_nil() || other.is_nil()) {
    return nil();
  }
  // Any other non-numeric operand (string / invalid / ref) is illegal → nil.
  if (!is_numeric() || !other.is_numeric()) {
    return nil();
  }

  int16_t rsz  = std::max(size, other.size);
  auto    dlop = make_result(Type::Integer, rsz);

  if (rsz == 1 && size == 1 && other.size == 1) {
    int64_t b = base()[0] ^ other.base()[0];
    int64_t e = 0;
    if (!(extra()[0] == 0 && other.extra()[0] == 0)) {
      e  = extra()[0] | other.extra()[0];
      b |= e;
    }
    dlop->set_word_pair(b, e);
  } else {
    const int64_t *s1 = base(), *s2 = other.base();
    const int64_t *e1 = extra(), *e2 = other.extra();
    int64_t *      s1_ext = nullptr, *s2_ext = nullptr, *e1_ext = nullptr, *e2_ext = nullptr;

    if (size < rsz) {
      s1_ext = alloc(rsz);
      e1_ext = alloc(rsz);
      Blop::extend(s1_ext, rsz, base()[size - 1] < 0 ? -1 : 0);
      Blop::extend(e1_ext, rsz, extra()[size - 1] < 0 ? -1 : 0);
      for (int i = 0; i < size; ++i) {
        s1_ext[i] = base()[i];
        e1_ext[i] = extra()[i];
      }
      s1 = s1_ext;
      e1 = e1_ext;
    }
    if (other.size < rsz) {
      s2_ext = alloc(rsz);
      e2_ext = alloc(rsz);
      Blop::extend(s2_ext, rsz, other.base()[other.size - 1] < 0 ? -1 : 0);
      Blop::extend(e2_ext, rsz, other.extra()[other.size - 1] < 0 ? -1 : 0);
      for (int i = 0; i < other.size; ++i) {
        s2_ext[i] = other.base()[i];
        e2_ext[i] = other.extra()[i];
      }
      s2 = s2_ext;
      e2 = e2_ext;
    }

    Blop::xorn(dlop->base(), rsz, s1, s2);
    if (!has_extra() && !other.has_extra()) {
      dlop->zero_extra();
    } else {
      int64_t* re = dlop->extra_mut();
      int64_t* rb = dlop->base();
      Blop::orn(re, rsz, e1, e2);
      Blop::orn(rb, rsz, rb, re);
    }

    if (s1_ext) {
      free(rsz, s1_ext);
      free(rsz, e1_ext);
    }
    if (s2_ext) {
      free(rsz, s2_ext);
      free(rsz, e2_ext);
    }
  }

  return dlop;
}

spool_ptr<Dlop> Dlop::not_op() const {
  // Nil propagates: NOT of an unset value is still unset.
  if (is_nil()) {
    return nil();
  }
  // Any other non-numeric operand (string / invalid / ref) is illegal → nil.
  if (!is_numeric()) {
    return nil();
  }

  auto dlop = make_result(Type::Integer, size);

  if (size == 1) {
    // NOT with unknowns: known bits flip, unknown bits stay unknown
    int64_t e = extra()[0];
    int64_t b = ~base()[0] | e;
    dlop->set_word_pair(b, e);
  } else {
    Blop::notn(dlop->base(), size, base());
    if (!has_extra()) {
      dlop->zero_extra();
    } else {
      int64_t* re = dlop->extra_mut();
      int64_t* rb = dlop->base();
      memcpy(re, extra(), size * sizeof(int64_t));
      Blop::orn(rb, size, rb, re);
    }
  }

  return dlop;
}

// =========================================================================
// Shift operations
// =========================================================================
spool_ptr<Dlop> Dlop::shl_op(int64_t amount) const {
  if (size <= 0) {
    return nil();  // no payload to shift (nil / invalid)
  }
  if (amount < 0) {
    return nil();  // negative shift amount is illegal (would trip Blop::shln)
  }
  if (amount == 0) {
    auto dlop = make_result(Type::Integer, size);
    dlop->copy_payload_from(*this);
    return dlop;
  }

  // A non-zero value shifted left by an astronomical amount cannot be
  // materialized (the result width is unbounded and would overflow the int16_t
  // word count). Reject it as nil rather than thrash the allocator. The cap is
  // the representational limit (INT16_MAX words ≈ 2M bits), far above any real
  // constant width.
  int64_t extra_words = (amount + 63) / 64;
  if (extra_words > static_cast<int64_t>(INT16_MAX) - size) {
    return nil();
  }
  int16_t rsz  = static_cast<int16_t>(size + extra_words);
  auto    dlop = make_result(Type::Integer, rsz);

  // Sign-extend source into larger buffer, then shift
  int64_t* tmp_base  = alloc(rsz);
  int64_t* tmp_extra = alloc(rsz);
  Blop::extend(tmp_base, rsz, base()[size - 1] < 0 ? -1 : 0);
  Blop::extend(tmp_extra, rsz, extra()[size - 1] < 0 ? -1 : 0);
  for (int i = 0; i < size; ++i) {
    tmp_base[i]  = base()[i];
    tmp_extra[i] = extra()[i];
  }

  Blop::shln(dlop->base(), rsz, tmp_base, amount);
  if (has_extra()) {
    Blop::shln(dlop->extra_mut(), rsz, tmp_extra, amount);
  } else {
    dlop->zero_extra();
  }

  free(rsz, tmp_base);
  free(rsz, tmp_extra);

  dlop->normalize();
  return dlop;
}

spool_ptr<Dlop> Dlop::sra_op(int64_t amount) const {
  if (size <= 0 || amount < 0) {
    // A payload-less value (nil) or a negative amount has no shift result;
    // nil instead of tripping Blop::shrn's asserts.
    return nil();
  }
  if (amount == 0) {
    auto dlop = make_result(Type::Integer, size);
    dlop->copy_payload_from(*this);
    return dlop;
  }

  auto dlop = make_result(Type::Integer, size);

  if (size == 1) {
    int64_t b = 0;
    int64_t e = 0;
    Blop::shrn(&b, 1, base(), amount);
    if (has_extra()) {
      Blop::shrn(&e, 1, extra(), amount);
    }
    dlop->set_word_pair(b, e);
  } else {
    Blop::shrn(dlop->base(), size, base(), amount);
    if (has_extra()) {
      Blop::shrn(dlop->extra_mut(), size, extra(), amount);
    } else {
      dlop->zero_extra();
    }
  }

  dlop->normalize();
  return dlop;
}

// Dlop-typed shift wrappers: forward to the int64 form once the amount is
// confirmed numeric and known. Unknown-amount widths follow eval.hpp's
// convention — left shift widens conservatively by 64 (could grow), right
// shift keeps the source width (cannot grow). Non-numeric / nil amount is
// invalid.
spool_ptr<Dlop> Dlop::shl_op(const Dlop& amount) const {
  if (amount.has_unknowns()) {
    return unknown(get_bits() + 64);
  }
  // The amount must be a plain (non-negative, in-range) integer. A non-numeric
  // amount — string / nil / invalid / ref — is illegal → nil. (is_just_i64 by
  // itself would accept a ref, whose Invalid type still packs i64-looking bits.)
  if (!amount.is_numeric() || !amount.is_just_i64()) {
    return nil();
  }
  return shl_op(amount.to_just_i64());
}

spool_ptr<Dlop> Dlop::sra_op(const Dlop& amount) const {
  if (amount.has_unknowns()) {
    return unknown(get_bits());
  }
  // Non-numeric amount (string / nil / invalid / ref) is illegal → nil.
  if (!amount.is_numeric() || !amount.is_just_i64()) {
    return nil();
  }
  return sra_op(amount.to_just_i64());
}

// =========================================================================
// Multiplexers / LUT
// =========================================================================
spool_ptr<Dlop> Dlop::clone(const Dlop& src) {
  auto dlop = make_result(src.type, src.size);
  dlop->copy_payload_from(src);
  dlop->normalize();
  return dlop;
}

bool Dlop::unknown_bit_test(int pos) const {
  if (size <= 0) {
    return false;  // nil / invalid empty value: treat as all-known-zero
  }
  int word = pos / 64;
  int bit  = pos % 64;
  if (word >= size) {
    return extra()[size - 1] < 0;  // sign extension of the unknown mask
  }
  return (extra()[word] >> bit) & 1;
}

spool_ptr<Dlop> Dlop::merge_unknown(const std::vector<const Dlop*>& cands) {
  assert(!cands.empty());
  if (cands.size() == 1) {
    return clone(*cands[0]);
  }

  int width = 1;
  for (const auto* c : cands) {
    width = std::max(width, c->get_bits());
  }
  int16_t words = static_cast<int16_t>((width + 63) / 64);
  if (words < 1) {
    words = 1;
  }

  auto result = make_result(Type::Integer, words);  // reconstruct zeroes both planes
  int64_t* re = result->extra_mut();
  int64_t* rb = result->base();

  for (int b = 0; b < words * 64; ++b) {
    bool any_unknown = false;
    bool has_known   = false;
    bool known_val   = false;
    bool disagree    = false;
    for (const auto* c : cands) {
      if (c->unknown_bit_test(b)) {
        any_unknown = true;
      } else {
        bool v = c->bit_test(b);
        if (!has_known) {
          has_known = true;
          known_val = v;
        } else if (v != known_val) {
          disagree = true;
        }
      }
    }
    bool unknown_here = any_unknown || disagree || !has_known;
    int  w            = b / 64;
    int  bit          = b % 64;
    if (unknown_here) {
      // Invariant: base bit is forced set wherever the value is unknown.
      re[w] |= int64_t(1) << bit;
      rb[w] |= int64_t(1) << bit;
    } else if (known_val) {
      rb[w] |= int64_t(1) << bit;
    }
  }

  result->normalize();
  return result;
}

spool_ptr<Dlop> Dlop::mux_op(const Dlop& sel, std::span<const spool_ptr<Dlop>> values) {
  assert(!values.empty());

  // True iff index `i` is consistent with sel's known/unknown bit pattern:
  // every *known* selector bit must match the corresponding bit of `i`.
  auto consistent = [&](size_t i) -> bool {
    int hib = 0;
    for (size_t t = i; t != 0; t >>= 1) {
      ++hib;
    }
    int maxb = std::max({sel.get_bits(), hib, 1});
    for (int b = 0; b < maxb; ++b) {
      if (sel.unknown_bit_test(b)) {
        continue;
      }
      bool sb = sel.bit_test(b);
      bool ib = (b < 64) && ((i >> b) & 1);
      if (sb != ib) {
        return false;
      }
    }
    return true;
  };

  if (!sel.has_unknowns()) {
    if (!sel.is_just_i64()) {
      return invalid();
    }
    int64_t idx = sel.to_just_i64();
    if (idx < 0 || static_cast<size_t>(idx) >= values.size()) {
      return invalid();
    }
    return clone(*values[idx]);
  }

  std::vector<const Dlop*> cands;
  for (size_t i = 0; i < values.size(); ++i) {
    if (consistent(i)) {
      cands.push_back(&*values[i]);
    }
  }
  if (cands.empty()) {
    return invalid();
  }
  return merge_unknown(cands);
}

spool_ptr<Dlop> Dlop::hotmux_op(const Dlop& sel, std::span<const spool_ptr<Dlop>> values) {
  assert(!values.empty());

  // A non-numeric selector (string / nil / invalid / ref) is illegal → nil.
  if (!sel.is_numeric()) {
    return nil();
  }

  int scan = std::max(sel.get_bits(), static_cast<int>(values.size()));

  int known_set   = -1;
  int known_count = 0;
  for (int b = 0; b < scan; ++b) {
    if (!sel.unknown_bit_test(b) && sel.bit_test(b)) {
      ++known_count;
      known_set = b;
    }
  }
  // Selector must be one-hot. More than one known-set bit is an illegal
  // selector → nil (formerly an assertion failure).
  if (known_count > 1) {
    return nil();
  }

  if (known_count == 1) {
    if (static_cast<size_t>(known_set) >= values.size()) {
      return invalid();
    }
    return clone(*values[known_set]);
  }

  // No known-set bit: the hot bit is among the unknown positions in range.
  std::vector<const Dlop*> cands;
  for (size_t b = 0; b < values.size(); ++b) {
    if (sel.unknown_bit_test(static_cast<int>(b))) {
      cands.push_back(&*values[b]);
    }
  }
  if (cands.empty()) {
    return invalid();  // selector is a known zero -> not one-hot
  }
  return merge_unknown(cands);
}

spool_ptr<Dlop> Dlop::lut_op(const Dlop& table, const Dlop& addr) {
  if (!addr.has_unknowns()) {
    if (!addr.is_just_i64()) {
      return invalid();
    }
    int64_t idx = addr.to_just_i64();
    if (idx < 0) {
      return invalid();
    }
    return create_bool(table.bit_test(static_cast<int>(idx)));
  }

  // Unknown address: fold every reachable table bit. Indices at or beyond the
  // table width all read the table's sign bit (a single constant), so one
  // representative covers that whole region.
  int tbits = std::max(table.get_bits(), 1);

  auto addr_consistent = [&](int idx) -> bool {
    int maxb = std::max({addr.get_bits(), 32, 1});
    for (int b = 0; b < maxb; ++b) {
      if (addr.unknown_bit_test(b)) {
        continue;
      }
      bool ab = addr.bit_test(b);
      bool ib = (b < 31) && ((idx >> b) & 1);
      if (ab != ib) {
        return false;
      }
    }
    return true;
  };

  bool has_known      = false;
  bool known_val      = false;
  bool unknown_result = false;
  for (int idx = 0; idx < tbits && !unknown_result; ++idx) {
    if (!addr_consistent(idx)) {
      continue;
    }
    bool v = table.bit_test(idx);
    if (!has_known) {
      has_known = true;
      known_val = v;
    } else if (v != known_val) {
      unknown_result = true;
    }
  }

  // Does the unknown pattern admit an index at or beyond the table width? Set
  // every unknown low bit to 1; if the maximum reachable index reaches tbits
  // the sign region is in play.
  if (!unknown_result) {
    uint64_t maxidx = 0;
    for (int b = 0; b < 31; ++b) {
      bool bit = addr.unknown_bit_test(b) || addr.bit_test(b);
      if (bit) {
        maxidx |= uint64_t(1) << b;
      }
    }
    if (maxidx >= static_cast<uint64_t>(tbits)) {
      bool v = table.bit_test(tbits);  // sign region
      if (!has_known) {
        has_known = true;
        known_val = v;
      } else if (v != known_val) {
        unknown_result = true;
      }
    }
  }

  if (!has_known || unknown_result) {
    return unknown(1);
  }
  return create_bool(known_val);
}

// =========================================================================
// Comparison operations
// =========================================================================
spool_ptr<Dlop> Dlop::eq_op(const Dlop& other) const {
  // Three-valued: a bit position where BOTH sides are known but the values
  // differ decides the result as false even when other positions carry
  // unknowns. Only collapse to unknown when every known/known position agrees
  // and at least one position is unknown.
  int16_t rsz = std::max(size, other.size);

  const int64_t b_sign  = (size > 0 && base()[size - 1] < 0) ? -1 : 0;
  const int64_t e_sign  = (size > 0 && extra()[size - 1] < 0) ? -1 : 0;
  const int64_t ob_sign = (other.size > 0 && other.base()[other.size - 1] < 0) ? -1 : 0;
  const int64_t oe_sign = (other.size > 0 && other.extra()[other.size - 1] < 0) ? -1 : 0;

  bool any_unknown = false;
  for (int i = 0; i < rsz; ++i) {
    int64_t b1 = (i < size) ? base()[i] : b_sign;
    int64_t e1 = (i < size) ? extra()[i] : e_sign;
    int64_t b2 = (i < other.size) ? other.base()[i] : ob_sign;
    int64_t e2 = (i < other.size) ? other.extra()[i] : oe_sign;

    // Bits known on BOTH sides — where (e1 | e2) is 0.
    int64_t known_both = ~(e1 | e2);
    if ((b1 & known_both) != (b2 & known_both)) {
      return create_bool(false);
    }
    if ((e1 | e2) != 0) {
      any_unknown = true;
    }
  }

  if (any_unknown) {
    return unknown_bool();
  }
  return create_bool(true);
}

// same_repr: structural/bitwise equality of (type, base, extra). Two values
// with identical unknown patterns compare equal — required for use as a key in
// hash maps, std::find, and dedup pools. This is NOT a semantic equality:
// for "values that would be equal once unknowns are resolved" use eq_op (three-
// valued) or is_known_eq (collapses unknowns to false).
bool Dlop::same_repr(const Dlop& other) const {
  if (type != other.type) {
    return false;
  }
  int16_t rsz       = std::max(size, other.size);
  bool    self_ext  = has_extra();
  bool    other_ext = other.has_extra();
  for (int i = 0; i < rsz; ++i) {
    int64_t b1 = (i < size) ? base()[i] : (base()[size - 1] < 0 ? -1 : 0);
    int64_t b2 = (i < other.size) ? other.base()[i] : (other.base()[other.size - 1] < 0 ? -1 : 0);
    if (b1 != b2) {
      return false;
    }
    int64_t e1 = self_ext && i < size ? extra()[i] : 0;
    int64_t e2 = other_ext && i < other.size ? other.extra()[i] : 0;
    if (e1 != e2) {
      return false;
    }
  }
  return true;
}

bool Dlop::is_known_eq(const Dlop& other) const { return eq_op(other)->is_known_true(); }

// Three-valued comparison: MSB→LSB walk that decides on the first known/known
// disagreement and gives up to unknown at the first unknown bit reached before
// any disagreement. Signed: at the top sign-extended bit, a known 1 means
// "more negative" than a known 0; below the sign bit, the usual unsigned bit
// weighting applies.
enum class CmpResult { Less, Equal, Greater, Unknown };

static CmpResult three_way_cmp(const Dlop& a, const Dlop& b) {
  int16_t rsz = std::max(a.size, b.size);
  if (rsz <= 0) {
    return CmpResult::Equal;
  }

  auto sign_b = [](const Dlop& d) -> int64_t {
    if (d.size <= 0) {
      return 0;
    }
    return d.base()[d.size - 1] < 0 ? -1 : 0;
  };
  auto sign_e = [](const Dlop& d) -> int64_t {
    if (d.size <= 0) {
      return 0;
    }
    return d.extra()[d.size - 1] < 0 ? -1 : 0;
  };

  const int64_t a_bs = sign_b(a);
  const int64_t a_es = sign_e(a);
  const int64_t b_bs = sign_b(b);
  const int64_t b_es = sign_e(b);

  const int top_bit_pos = rsz * 64 - 1;

  // Walk from highest bit to lowest. Words are processed high→low; within each
  // word we extract the topmost "interesting" bit (known-disagreement or
  // unknown) via __builtin_clzll on the merged mask.
  for (int w = rsz - 1; w >= 0; --w) {
    int64_t b1 = (w < a.size) ? a.base()[w] : a_bs;
    int64_t e1 = (w < a.size) ? a.extra()[w] : a_es;
    int64_t b2 = (w < b.size) ? b.base()[w] : b_bs;
    int64_t e2 = (w < b.size) ? b.extra()[w] : b_es;

    int64_t  combined_extra = e1 | e2;
    // diff_known: bits where both sides are known AND they disagree.
    int64_t  diff_known     = (b1 ^ b2) & ~combined_extra;
    uint64_t interesting    = static_cast<uint64_t>(diff_known | combined_extra);

    while (interesting != 0) {
      int      bit  = 63 - __builtin_clzll(interesting);  // highest set bit
      uint64_t mask = uint64_t(1) << bit;
      bool     unk  = (combined_extra & static_cast<int64_t>(mask)) != 0;
      if (unk) {
        return CmpResult::Unknown;
      }
      // Known disagreement at this bit.
      int  p      = w * 64 + bit;
      bool b1_set = (b1 & static_cast<int64_t>(mask)) != 0;
      bool b2_set = (b2 & static_cast<int64_t>(mask)) != 0;
      // Equal case excluded by diff_known.
      bool a_less;
      if (p == top_bit_pos) {
        // Sign bit position: 1 → negative, so set side is the smaller one.
        a_less = b1_set && !b2_set;
      } else {
        a_less = !b1_set && b2_set;
      }
      return a_less ? CmpResult::Less : CmpResult::Greater;
    }
  }
  return CmpResult::Equal;
}

spool_ptr<Dlop> Dlop::lt_op(const Dlop& other) const {
  auto r = three_way_cmp(*this, other);
  switch (r) {
    case CmpResult::Less   : return create_bool(true);
    case CmpResult::Equal  : return create_bool(false);
    case CmpResult::Greater: return create_bool(false);
    case CmpResult::Unknown: return unknown_bool();
  }
  return unknown_bool();
}
spool_ptr<Dlop> Dlop::le_op(const Dlop& other) const {
  auto r = three_way_cmp(*this, other);
  switch (r) {
    case CmpResult::Less   : return create_bool(true);
    case CmpResult::Equal  : return create_bool(true);
    case CmpResult::Greater: return create_bool(false);
    case CmpResult::Unknown: return unknown_bool();
  }
  return unknown_bool();
}
spool_ptr<Dlop> Dlop::gt_op(const Dlop& other) const {
  auto r = three_way_cmp(*this, other);
  switch (r) {
    case CmpResult::Less   : return create_bool(false);
    case CmpResult::Equal  : return create_bool(false);
    case CmpResult::Greater: return create_bool(true);
    case CmpResult::Unknown: return unknown_bool();
  }
  return unknown_bool();
}
spool_ptr<Dlop> Dlop::ge_op(const Dlop& other) const {
  auto r = three_way_cmp(*this, other);
  switch (r) {
    case CmpResult::Less   : return create_bool(false);
    case CmpResult::Equal  : return create_bool(true);
    case CmpResult::Greater: return create_bool(true);
    case CmpResult::Unknown: return unknown_bool();
  }
  return unknown_bool();
}

// =========================================================================
// Bit manipulation
// =========================================================================
// Dlop-typed sext wrapper: forward to the int form once the bit count is
// confirmed numeric and known. Non-numeric / unknown bit count is invalid.
spool_ptr<Dlop> Dlop::sext_op(const Dlop& bits) const {
  // Sign-extending a non-number, or by a non-numeric / unknown / out-of-range
  // bit count, is illegal → nil. (is_just_i64 alone would accept a ref.)
  if (!is_numeric()) {
    return nil();
  }
  if (!bits.is_numeric() || !bits.is_just_i64()) {
    return nil();
  }
  return sext_op(static_cast<int>(bits.to_just_i64()));
}

spool_ptr<Dlop> Dlop::sext_op(int from_bit) const {
  if (size <= 0) {
    return nil();  // no payload (nil / invalid)
  }
  if (from_bit < 0) {
    return nil();  // illegal bit position (would trip Blop::sext64/sextn)
  }
  // A sign-extend point at or beyond the stored width is a no-op — those bits
  // are already the sign extension — and dodges Blop::sext64's from_bit < 64
  // precondition for single-word values.
  if (from_bit >= size * 64) {
    auto dlop = make_result(Type::Integer, size);
    dlop->copy_payload_from(*this);
    dlop->normalize();
    return dlop;
  }
  auto dlop = make_result(Type::Integer, size);

  // Sign-extend both base AND extra from `from_bit`. If the sign bit is
  // unknown (extra bit set) the extension must also be unknown — copying
  // extra verbatim would leave bits above from_bit holding whatever the
  // source had, instead of the sign-extended unknown.
  if (size == 1) {
    int64_t b = 0;
    int64_t e = 0;
    Blop::sext64(b, base()[0], from_bit);
    Blop::sext64(e, extra()[0], from_bit);
    // Maintain invariant: unknown bits have base = 1.
    dlop->set_word_pair(b | e, e);
  } else {
    int64_t* re = dlop->extra_mut();
    int64_t* rb = dlop->base();
    Blop::sextn(rb, size, base(), from_bit);
    Blop::sextn(re, size, extra(), from_bit);
    // Maintain invariant: unknown bits have base = 1.
    for (int i = 0; i < size; ++i) {
      rb[i] |= re[i];
    }
  }

  dlop->normalize();
  return dlop;
}

spool_ptr<Dlop> Dlop::get_mask_op() const {
  // Convert a signed value to its unsigned magnitude bit pattern. A
  // non-negative value is already its own mask.
  if (!is_negative()) {
    auto dlop = make_result(Type::Integer, size);
    dlop->copy_payload_from(*this);
    return dlop;
  }

  // Negative: mask = (1 << nbits) + value, i.e. clear every bit at and above
  // bit `nbits`, leaving a NON-NEGATIVE result (matches Lconst::get_mask_op).
  // words = nbits/64 + 1 always keeps one clear word of headroom above the top
  // kept bit so the result's sign bit is 0 (it is unsigned), including the
  // nbits-is-a-multiple-of-64 boundary where the top word must be fully zeroed.
  int      nbits = get_bits();
  int      words = nbits / 64 + 1;
  auto     dlop  = make_result(Type::Integer, static_cast<int16_t>(words));
  // extra_mut() may materialize heap storage for a size-1 result, moving base out
  // of the inline word — so it must be called BEFORE caching base() (see dlop.hpp).
  int64_t* re    = dlop->extra_mut();
  int64_t* rb    = dlop->base();
  for (int i = 0; i < words; ++i) {
    rb[i] = (i < size) ? base()[i]  : -1;  // a negative value sign-extends base with 1s
    re[i] = (i < size) ? extra()[i] : 0;   // unknown plane: no unknowns above stored width
  }
  int top_word = nbits / 64;  // word holding bit `nbits`
  int top_bit  = nbits % 64;
  if (top_bit == 0) {
    rb[top_word] = 0;  // bit `nbits` is at a word boundary: clear the whole word
    re[top_word] = 0;
  } else {
    int64_t m    = static_cast<int64_t>((uint64_t(1) << top_bit) - 1);
    rb[top_word] &= m;
    re[top_word] &= m;
  }
  for (int i = top_word + 1; i < words; ++i) {
    rb[i] = 0;
    re[i] = 0;
  }
  // Preserve the unknown plane (do NOT zero_extra): the magnitude keeps the
  // source's unknown bits below `nbits`, matching Lconst::get_mask_op. Masking
  // both planes with the same mask keeps the "unknown bits have base==1" invariant.
  dlop->normalize();
  return dlop;
}

// get_mask_op(mask): copy the bits of `*this` selected by `mask` into a new
// integer, packed into the low bits in their original order. Negative mask
// means "all bits except the lowest |mask| bits" (matches Lconst semantics).
//
// Examples (mirroring lconst.cpp comment header):
//   get_mask(0xfeed, 0xff)  -> 0xed
//   get_mask(0xfeed, -16)   -> 0     (all bits beyond bit 15)
//   get_mask(0xfeed, 0xf00) -> 0xe
//
// Single-bit result: when exactly one bit is selected, the result is the
// signed 1-bit integer -1 (bit set) or 0 (bit clear) — not 0sb01. This
// matches Pyrope's `x#[i]` semantics. We detect this from the selected-bit
// count after the loop, so no separate popcount/bit-find pass is needed.
spool_ptr<Dlop> Dlop::get_mask_op(const Dlop& mask) const {
  // Note: mask == -1 is NOT special-cased to the no-arg sign-strip form. They
  // differ when exactly one bit is selected — get_mask_op(mask) applies the
  // single-bit rule (a lone selected set bit yields the signed -1, not 1),
  // e.g. get_mask(-1, -1) == -1 whereas the sign-strip get_mask_op() gives 1.
  // The general path below handles an all-ones mask correctly.
  if (mask.has_unknowns()) {
    // Mask itself has unknown bits: every output bit is potentially unknown
    // because we can't tell which source bits get extracted. Width is bounded
    // by the mask's bit count (positive mask) — return a sound unknown of
    // that width rather than collapsing to Invalid.
    int w = mask.get_bits();
    if (w <= 0) {
      w = 1;
    }
    return unknown(w);
  }

  // Determine effective range: positive mask uses its bit width; a negative
  // mask carves out everything ABOVE its bit width (sign extension picks up).
  int  mask_bits          = mask.get_bits();
  bool mask_neg           = mask.is_negative();
  int  src_bits           = get_bits();
  int  positive_mask_bits = mask_neg ? (mask_bits - 1) : mask_bits;

  // Pre-size the output. A negative mask packs every selected bit in
  // [0, positive_mask_bits) plus the carve-out region [positive_mask_bits,
  // src_bits); when the mask is wider than the source the first range
  // dominates, so the bound is max(src_bits, positive_mask_bits). A positive
  // mask packs at most positive_mask_bits bits. The extracted bits may include
  // the source's sign bits (positions >= src_bits read the two's-complement
  // sign), so the bound must NOT cap at src_bits.
  int max_out_bits = mask_neg ? std::max(src_bits, positive_mask_bits) : positive_mask_bits;
  // One extra zero word above the top output bit keeps the packed result
  // non-negative (it is an unsigned bit-extract), matching Slop's fixed-width
  // result; normalize() shrinks it afterward.
  int  out_words = std::max(1, (max_out_bits + 63) / 64 + 1);
  auto result    = make_result(Type::Integer, static_cast<int16_t>(out_words));  // zeroed by reconstruct
  int64_t* re    = result->extra_mut();
  int64_t* rb    = result->base();

  // copy_bit: write source bit at position `i` into result at `out_bit`,
  // carrying the unknown flag from extra() so an unknown source bit stays
  // unknown in the output (invariant: unknown bits have base=1). Positions at
  // or beyond the stored width read the sign bit (handled below), matching the
  // sign-extending bit_test() Slop uses.
  auto copy_bit = [&](int i, int out) {
    int  sword = i / 64;
    int  sbit  = i % 64;
    int  oword = out / 64;
    int  obit  = out % 64;
    // Beyond the stored words the source is its sign bit (two's complement):
    // 1 for a negative value, 0 otherwise. Unknown bits never extend past size.
    bool b     = (sword < size) ? ((base()[sword] >> sbit) & 1) : is_negative();
    bool u     = (sword < size) ? ((extra()[sword] >> sbit) & 1) : false;
    if (b || u) {
      rb[oword] |= int64_t(1) << obit;
    }
    if (u) {
      re[oword] |= int64_t(1) << obit;
    }
  };

  int out_bit = 0;
  for (int i = 0; i < positive_mask_bits; ++i) {
    bool selected = mask_neg ? !mask.bit_test(i) : mask.bit_test(i);
    if (!selected) {
      continue;
    }
    copy_bit(i, out_bit);
    ++out_bit;
  }
  if (mask_neg) {
    for (int i = positive_mask_bits; i < src_bits; ++i) {
      copy_bit(i, out_bit);
      ++out_bit;
    }
  }
  // Exactly one bit selected → return the signed 1-bit integer -1 or 0
  // (or a 1-bit unknown if that bit was ?), not the unsigned 1 / 0 the
  // loop just packed.
  if (out_bit == 1) {
    if (result->extra()[0] & 1) {
      return unknown(1);
    }
    return create_integer((result->base()[0] & 1) ? -1 : 0);
  }
  result->normalize();
  return result;
}

// set_mask_op(mask, value): replace the bits of `*this` selected by `mask`
// with bits taken LSB-first from `value`; bits not selected stay as-is.
// Mirrors Lconst::set_mask_op semantics, but only handles the
// non-unknown integer case (mask must not have unknowns).
//
//   set_mask(0xFFF, 0xF, 0xa) -> 0xFFa
//   set_mask(0xFFF, -16, 0xa) -> 0x0aF
//   set_mask(foo, -1, bar)    -> bar
//   set_mask(foo,  0, bar)    -> foo
spool_ptr<Dlop> Dlop::set_mask_op(const Dlop& mask, const Dlop& value) const {
  // Illegal/degenerate operands → nil: base/mask/value must be usable numbers,
  // and the mask must be fully known (an unknown mask cannot select definite
  // bit positions — this replaces the former assert(!mask.has_unknowns())).
  if (!is_numeric() || !mask.is_numeric() || !value.is_numeric() || mask.has_unknowns()) {
    return nil();
  }
  if (mask.is_known_false()) {
    auto dlop = make_result(Type::Integer, size);
    dlop->copy_payload_from(*this);
    return dlop;
  }
  // mask == -1: fully replaced
  if (mask.is_negative()) {
    bool all_ones = true;
    for (int i = 0; i < mask.size; ++i) {
      if (mask.base()[i] != -1) {
        all_ones = false;
        break;
      }
    }
    if (all_ones) {
      auto dlop = make_result(Type::Integer, value.size);
      dlop->copy_payload_from(value);
      return dlop;
    }
  }

  bool mask_neg           = mask.is_negative();
  int  mask_bits          = mask.get_bits();
  int  positive_mask_bits = mask_neg ? (mask_bits - 1) : mask_bits;

  // out_bits: enough to hold base bits and (for negative masks) the value
  // bits past mask_bits.
  int out_bits = std::max(get_bits(), mask_bits);
  if (mask_neg) {
    out_bits = std::max(out_bits, positive_mask_bits + value.get_bits());
  }

  // One word of headroom above the top filled bit so the result's sign stays
  // the SOURCE's sign extension (bits past out_bits are untouched, like Slop's
  // fixed-width result) rather than a value bit landing at a word's top bit and
  // flipping the sign. normalize() trims it afterward.
  int      out_words = std::max(1, out_bits / 64 + 1);
  auto     result    = make_result(Type::Integer, static_cast<int16_t>(out_words));
  int64_t* re        = result->extra_mut();
  int64_t* rb        = result->base();
  // Start from `this`, sign-extended to out_words. Bits not selected by the
  // mask (including the sign-extension region beyond get_bits()) flow through
  // unchanged; the loop overwrites only the mask-selected positions.
  int64_t base_sign  = (size > 0 && base()[size - 1] < 0) ? -1 : 0;
  int64_t extra_sign = (size > 0 && extra()[size - 1] < 0) ? -1 : 0;
  for (int i = 0; i < out_words; ++i) {
    rb[i] = (i < size) ? base()[i] : base_sign;
    re[i] = (i < size) ? extra()[i] : extra_sign;
  }

  // tri_bit: returns (base_bit, extra_bit) at position `p` from a Dlop.
  auto tri_bit = [](const Dlop& d, int p) -> std::pair<bool, bool> {
    int  word = p / 64;
    int  bit  = p % 64;
    bool b, u;
    if (word >= d.size) {
      b = d.base()[d.size - 1] < 0;
      u = d.extra()[d.size - 1] < 0;
    } else {
      b = (d.base()[word] >> bit) & 1;
      u = (d.extra()[word] >> bit) & 1;
    }
    return {b, u};
  };

  int value_pos = 0;
  for (int i = 0; i < out_bits; ++i) {
    bool from_value;
    if (i < positive_mask_bits) {
      bool mb    = mask.bit_test(i);
      from_value = mask_neg ? !mb : mb;
    } else {
      from_value = mask_neg;
    }
    if (!from_value) {
      continue;  // bit stays from `this` (already in result).
    }
    auto p = tri_bit(value, value_pos);
    ++value_pos;
    int     w        = i / 64;
    int     s        = i % 64;
    int64_t bit_mask = int64_t(1) << s;
    if (p.first || p.second) {
      rb[w] |= bit_mask;
    } else {
      rb[w] &= ~bit_mask;
    }
    if (p.second) {
      re[w] |= bit_mask;
    } else {
      re[w] &= ~bit_mask;
    }
  }

  result->normalize();
  return result;
}

// ror_op: OR-reduce two operands to a single bit. Yields 1 if either side has
// any nonzero bit; otherwise 0. Matches Lconst::ror_op.
spool_ptr<Dlop> Dlop::ror_op(const Dlop& other) const {
  bool any = is_known_true() || other.is_known_true();
  return create_integer(any ? int64_t(1) : int64_t(0));
}

// ror_op (unary): OR-reduce this operand's bits, returning a Bool Dlop.
// Known-true if any bit is set; known-false only when every bit is provably
// zero. Pure unknowns collapse to a 1-bit unknown.
spool_ptr<Dlop> Dlop::ror_op() const {
  if (is_invalid() || is_nil() || is_string()) {
    return invalid();
  }
  if (is_known_true()) {
    return create_bool(true);
  }
  if (has_unknowns()) {
    return unknown_bool();
  }
  return create_bool(false);
}

// rand_op: AND-reduction (single operand). True iff every bit is set
// (i.e., the value is a 2^n-1 mask). Unknown bits → 1-bit unknown.
spool_ptr<Dlop> Dlop::rand_op() const {
  if (has_unknowns()) {
    return unknown_bool();
  }
  if (is_invalid() || is_nil() || is_string()) {
    return invalid();
  }
  return create_bool(is_mask());
}

// rxor_op: XOR-reduction (single operand). True iff popcount is odd.
// Unknown bits → 1-bit unknown.
spool_ptr<Dlop> Dlop::rxor_op() const {
  if (has_unknowns()) {
    return unknown_bool();
  }
  if (is_invalid() || is_nil() || is_string()) {
    return invalid();
  }
  return create_bool((popcount() & 1) == 1);
}

// popcount_op: number of set bits, returned as an Integer Dlop.
//
// With no unknowns this is the exact count. With unknowns the count is only
// bounded: every unknown bit independently contributes 0 or 1, so the true
// popcount lies anywhere in [ones, ones+u] where `ones` is the number of
// known-set bits (base & ~extra) and `u` the number of unknown bits (extra).
//
// We encode that contiguous range as the tightest ternary cube covering it:
// let lo=ones, hi=ones+u; the bits above the highest position where lo and hi
// differ form a fixed common prefix, and every bit at or below that position
// becomes unknown. This is a sound over-approximation — it never drops a real
// popcount value, though it may admit a few extra ones (e.g. [3,5] widens to
// 0ub??? = [0,7]).
spool_ptr<Dlop> Dlop::popcount_op() const {
  if (is_invalid() || is_nil() || is_string()) {
    return invalid();
  }

  // Popcount is only well-defined for non-negative finite values. A negative
  // value — or one whose sign bit is unknown (e.g. 0sb?...) — sign-extends with
  // unbounded set/unknown bits, so the count has no finite answer. Return a
  // generic 1-bit unknown (0sb?). By the base == base|extra invariant an
  // unknown sign bit also forces base's top bit to 1, so is_negative() catches
  // both the negative and the unknown-sign cases.
  if (is_negative()) {
    return unknown(1);
  }

  int ones = 0;  // known-set bits: base bit set and not unknown
  int u    = 0;  // unknown bits
  for (int i = 0; i < size; ++i) {
    uint64_t b  = static_cast<uint64_t>(base()[i]);
    uint64_t e  = static_cast<uint64_t>(extra()[i]);
    ones       += __builtin_popcountll(b & ~e);
    u          += __builtin_popcountll(e);
  }

  if (u == 0) {
    return create_integer(ones);  // exact, fully known
  }

  // lo/hi fit in int64: popcount is bounded by the bit count of the value.
  int64_t lo   = ones;
  int64_t hi   = static_cast<int64_t>(ones) + u;
  int64_t diff = lo ^ hi;  // nonzero since u > 0 ⇒ hi > lo
  int     p    = 63 - __builtin_clzll(static_cast<uint64_t>(diff));
  int64_t mask = (p >= 63) ? ~int64_t(0) : ((int64_t(1) << (p + 1)) - 1);

  // Fixed common prefix above bit p, with bits [0..p] marked unknown. Unknown
  // bits must carry base=1 (the base == base|extra invariant), so OR mask into
  // both base and extra.
  auto result = create_integer(lo & ~mask);
  result->or_base(mask);
  result->or_extra(mask);
  result->normalize();
  return result;
}

spool_ptr<Dlop> Dlop::concat_op(const Dlop& other) const {
  // nil / invalid / ref has no bit pattern to concatenate → nil. (String and
  // numeric operands fall through to the text / bit-concat paths below.)
  if (is_nil() || other.is_nil() || is_invalid() || other.is_invalid()) {
    return nil();
  }
  // String ++ string is a *text* concat, not a numeric bit-concat.
  // init_string stores the first character in the low byte (so "ab" is
  // numerically `b'*256 + a` = 0x6261). To make "ab" ++ "cd" produce
  // "abcd" = 0x64636261, the second operand's bytes have to occupy the
  // HIGH region: result = (other << self_bits) | self — the opposite
  // shift direction of integer ++. The result must also stay
  // Type::String so subsequent passes don't misclassify the value as a
  // bit-packed integer.
  if (is_string() && other.is_string()) {
    // String width must be byte-aligned, not the generic signed-bit
    // count: a string like "hello " has 6*8=48 significant bits, but
    // get_bits() returns 47 (it doesn't know the value is byte-packed
    // and reserves a sign bit). Using the signed count here would
    // shift the second operand by 47 instead of 48 and corrupt the
    // bytes. Find the highest non-zero byte to recover the byte count.
    auto byte_count = [](const Dlop& s) -> int {
      for (int w = s.size - 1; w >= 0; --w) {
        uint64_t v = static_cast<uint64_t>(s.base()[w]);
        if (v == 0) {
          continue;
        }
        for (int b = 7; b >= 0; --b) {
          if (((v >> (b * 8)) & 0xFF) != 0) {
            return w * 8 + b + 1;
          }
        }
      }
      return 0;
    };
    int self_bits = byte_count(*this) * 8;
    if (self_bits <= 0) {
      auto dlop = make_result(Type::String, other.size);
      dlop->copy_payload_from(other);
      return dlop;
    }
    auto shifted     = other.shl_op(self_bits);
    auto masked_self = get_mask_op();
    auto r           = shifted->or_op(masked_self);
    r->type          = Type::String;
    return r;
  }

  int other_bits = other.get_bits();
  if (other_bits <= 0) {
    auto dlop = make_result(Type::Integer, size);
    dlop->copy_payload_from(*this);
    return dlop;
  }

  auto shifted      = shl_op(other_bits);
  auto masked_other = other.get_mask_op();
  return shifted->or_op(masked_other);
}

spool_ptr<Dlop> Dlop::adjust_bits(int amount) const {
  assert(amount > 0);
  // A non-numeric value (string / nil / invalid / ref) has no meaningful bit
  // adjustment → nil. Guarding also keeps size >= 1 for the kernels below
  // (a size-0 value would trip extra_mut()/heapify_set_extra()).
  if (!is_numeric()) {
    return nil();
  }
  auto dlop = make_result(Type::Integer, size);

  if (size == 1) {
    // Mask both planes: leaving the unknown (extra) bits above `amount` set would
    // both leak unknowns and break the "unknown bits have base==1" invariant.
    int64_t m = (amount < 64) ? static_cast<int64_t>((uint64_t(1) << amount) - 1) : -1;
    dlop->set_word_pair(base()[0] & m, extra()[0] & m);
  } else {
    int64_t* re = dlop->extra_mut();
    int64_t* rb = dlop->base();
    memcpy(rb, base(), size * sizeof(int64_t));
    memcpy(re, extra(), size * sizeof(int64_t));
    int top_word = amount / 64;
    int top_bit  = amount % 64;
    if (top_word < size) {
      if (top_bit == 0) {
        // `amount` on a word boundary: clear the whole word in both planes
        // (the previous code skipped it, leaking the high word).
        rb[top_word] = 0;
        re[top_word] = 0;
      } else {
        int64_t m = static_cast<int64_t>((uint64_t(1) << top_bit) - 1);
        rb[top_word] &= m;
        re[top_word] &= m;
      }
    }
    for (int i = top_word + 1; i < size; ++i) {
      rb[i] = 0;
      re[i] = 0;
    }
  }

  dlop->normalize();
  return dlop;
}

// =========================================================================
// Queries
// =========================================================================
bool Dlop::is_negative() const {
  if (size <= 0) {
    return false;
  }
  return base()[size - 1] < 0;
}

bool Dlop::is_positive() const {
  if (size <= 0) {
    return false;
  }
  return base()[size - 1] >= 0;
}

bool Dlop::is_known_false() const {
  if (has_unknowns()) {
    return false;
  }
  for (int i = 0; i < size; ++i) {
    if (base()[i] != 0) {
      return false;
    }
  }
  return true;
}

bool Dlop::is_known_zero() const {
  // Numeric zero: must be an Integer/Boolean (not Nil/Invalid/String), with no
  // unknown bits, all words zero. Matches the semantic of `value == 0` in
  // arithmetic contexts.
  if (type != Type::Integer && type != Type::Boolean) {
    return false;
  }
  if (has_unknowns()) {
    return false;
  }
  for (int i = 0; i < size; ++i) {
    if (base()[i] != 0) {
      return false;
    }
  }
  return true;
}

bool Dlop::is_known_true() const {
  if (has_unknowns()) {
    // If any known bit is 1, it's true
    for (int i = 0; i < size; ++i) {
      if ((base()[i] & ~extra()[i]) != 0) {
        return true;
      }
    }
    return false;
  }
  for (int i = 0; i < size; ++i) {
    if (base()[i] != 0) {
      return true;
    }
  }
  return false;
}

bool Dlop::is_ref() const {
  if (type != Type::Invalid) {
    return false;
  }
  if (size <= 0) {
    return false;
  }
  for (int i = 0; i < size; ++i) {
    if (base()[i] != 0) {
      return true;
    }
  }
  return false;
}

bool Dlop::is_mask() const {
  if (has_unknowns() || is_negative()) {
    return false;
  }
  // Check if value is (2^n - 1) for some n: all lower bits 1, rest 0
  // Equivalent to: (v + 1) & v == 0 and v != 0
  if (size == 1) {
    return base()[0] > 0 && ((base()[0] + 1) & base()[0]) == 0;
  }
  // Multi-word: add 1 and check if result & original == 0
  // For simplicity, use the bit pattern check
  int top = size - 1;
  while (top > 0 && base()[top] == 0) {
    --top;
  }
  // The highest non-zero word is the top of a positive value (is_negative was
  // excluded above), so read it UNSIGNED: an all-ones low word (0xFFFF…F, i.e.
  // -1 as int64) is the valid 64-bit run of `2^64-1`, not a negative word.
  uint64_t topw = static_cast<uint64_t>(base()[top]);
  if (topw == 0) {
    return false;  // the value is zero
  }

  // Check top word: must be (2^k - 1)
  if (((topw + 1) & topw) != 0) {
    return false;
  }
  // All lower words must be all-ones
  for (int i = 0; i < top; ++i) {
    if (base()[i] != -1) {
      return false;
    }
  }
  return true;
}

bool Dlop::is_power2() const {
  if (has_unknowns() || is_negative()) {
    return false;
  }
  if (size == 1) {
    return base()[0] > 0 && ((base()[0] - 1) & base()[0]) == 0;
  }
  // Exactly one bit set
  int nonzero_count = 0;
  int nonzero_idx   = -1;
  for (int i = 0; i < size; ++i) {
    if (base()[i] != 0) {
      ++nonzero_count;
      nonzero_idx = i;
    }
  }
  if (nonzero_count != 1) {
    return false;
  }
  // Read the lone non-zero word UNSIGNED: a positive value can carry its single
  // set bit at position 63 of a low word (word == 0x8000…0, i.e. INT64_MIN as
  // int64). Signed `w - 1` there is overflow UB; unsigned wraps correctly.
  uint64_t w = static_cast<uint64_t>(base()[nonzero_idx]);
  return ((w - 1) & w) == 0;
}

int Dlop::get_bits() const {
  if (size <= 0) {
    return 0;
  }
  int base_bits = (size == 1) ? Blop::get_bits64(base()[0]) : Blop::get_bitsn(base(), size);
  if (!has_unknowns()) {
    return base_bits;
  }

  // With unknowns: an unknown bit at position k can resolve to a value that
  // differs from the known sign extension, forcing the value to need k+2
  // bits (k+1 for the resolved bit plus 1 for the sign). Return an upper
  // bound across all concretizations so width-sensitive ops (concat,
  // set_mask, get_mask) agree with what Slop would compute on any
  // concretization.
  if (extra()[size - 1] < 0) {
    // Top stored bit is unknown → sign itself is unknown → conservative
    // upper bound from the stored width.
    return std::max(base_bits, size * 64 + 1);
  }
  int highest_unk = -1;
  for (int w = size - 1; w >= 0; --w) {
    if (extra()[w] != 0) {
      highest_unk = w * 64 + 63 - __builtin_clzll(static_cast<uint64_t>(extra()[w]));
      break;
    }
  }
  if (highest_unk < 0) {
    return base_bits;
  }
  return std::max(base_bits, highest_unk + 2);
}

bool Dlop::bit_test(int pos) const {
  if (size <= 0) {
    return false;  // nil / invalid empty value: treat as all-known-zero
  }
  int word = pos / 64;
  int bit  = pos % 64;
  if (word >= size) {
    return base()[size - 1] < 0;  // sign extension
  }
  return (base()[word] >> bit) & 1;
}

int Dlop::get_first_bit_set() const {
  for (int i = 0; i < size; ++i) {
    if (base()[i] != 0) {
      return i * 64 + __builtin_ctzll(static_cast<uint64_t>(base()[i]));
    }
  }
  return -1;
}

int Dlop::get_last_bit_set() const {
  for (int i = size - 1; i >= 0; --i) {
    if (static_cast<uint64_t>(base()[i]) != 0) {
      return i * 64 + 63 - __builtin_clzll(static_cast<uint64_t>(base()[i]));
    }
  }
  return -1;
}

int Dlop::popcount() const {
  int count = 0;
  for (int i = 0; i < size; ++i) {
    count += __builtin_popcountll(static_cast<uint64_t>(base()[i]));
  }
  return count;
}

int Dlop::get_trailing_zeroes() const {
  if (is_known_false()) {
    return 0;
  }
  for (int i = 0; i < size; ++i) {
    if (base()[i] != 0) {
      return i * 64 + __builtin_ctzll(static_cast<uint64_t>(base()[i]));
    }
  }
  return 0;
}

bool Dlop::is_just_i64() const {
  if (has_unknowns()) {
    return false;
  }
  return get_bits() <= 62;
}

int64_t Dlop::to_just_i64() const {
  assert(is_just_i64());
  return base()[0];
}

// =========================================================================
// Conversion to string formats
// =========================================================================
std::string Dlop::to_string() const {
  // Convert base to string (for String type)
  std::string str;
  if (size == 1) {
    uint64_t tmp = static_cast<uint64_t>(base()[0]);
    while (tmp) {
      str.push_back(static_cast<char>(tmp & 0xFF));
      tmp >>= 8;
    }
  } else {
    for (int w = 0; w < size; ++w) {
      uint64_t tmp = static_cast<uint64_t>(base()[w]);
      for (int b = 0; b < 8; ++b) {
        auto ch = static_cast<char>(tmp & 0xFF);
        if (ch == 0 && w == size - 1) {
          break;
        }
        str.push_back(ch);
        tmp >>= 8;
      }
    }
    // Remove trailing nulls
    while (!str.empty() && str.back() == '\0') {
      str.pop_back();
    }
  }
  return str;
}

std::string Dlop::to_binary() const {
  if (has_unknowns()) {
    // Emit binary with ? for unknown bits
    int nbits = get_bits();
    if (nbits <= 0) {
      nbits = 1;
    }
    std::string result;
    for (int i = nbits - 1; i >= 0; --i) {
      bool b          = bit_test(i);
      int  word       = i / 64;
      int  bit        = i % 64;
      bool is_unknown = (word < size) ? ((extra()[word] >> bit) & 1) : false;
      if (is_unknown) {
        result.push_back('?');
      } else {
        result.push_back(b ? '1' : '0');
      }
    }
    return result;
  }

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

std::string Dlop::to_pyrope() const {
  if (is_invalid()) {
    return "";
  }

  if (type == Type::String) {
    auto str = to_string();
    if (str.empty()) {
      return "''";
    }
    return std::format("'{}'", str);
  }

  if (type == Type::Boolean) {
    return is_known_true() ? "true" : "false";
  }

  if (has_unknowns()) {
    auto bin = to_binary();
    if (is_negative()) {
      return std::format("0sb{}", bin);
    }
    // Emit the explicit unsigned prefix — bare `0b…` is no longer valid pyrope
    // (from_pyrope rejects it), so to_pyrope must round-trip as `0ub…`.
    return std::format("0ub{}", bin);
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

  // Large number: convert to hex
  // For multi-word, output hex from MSW to LSW
  std::string result;
  if (is_negative()) {
    result   = "-0x";
    // Negate and print
    auto pos = neg_op();
    for (int i = pos->size - 1; i >= 0; --i) {
      if (i == pos->size - 1) {
        result += std::format("{:x}", static_cast<uint64_t>(pos->base()[i]));
      } else {
        result += std::format("{:016x}", static_cast<uint64_t>(pos->base()[i]));
      }
    }
  } else {
    result = "0x";
    for (int i = size - 1; i >= 0; --i) {
      if (i == size - 1) {
        result += std::format("{:x}", static_cast<uint64_t>(base()[i]));
      } else {
        result += std::format("{:016x}", static_cast<uint64_t>(base()[i]));
      }
    }
  }
  return result;
}

// Arbitrary-precision signed decimal. Manual long division of the raw
// magnitude words by 1e9 (Blop's div is 64-bit-only), so it handles values far
// wider than int64. Non-plain-integer values fall back to to_pyrope.
std::string Dlop::to_decimal_string() const {
  if (is_invalid() || type == Type::String || has_unknowns()) {
    return to_pyrope();
  }
  if (is_known_zero()) {
    return "0";
  }
  const bool      neg = is_negative();
  spool_ptr<Dlop> holder;
  const Dlop*     mag = this;
  if (neg) {
    holder = neg_op();
    mag    = holder.get();
  }
  // Magnitude as little-endian uint64 words.
  std::vector<uint64_t> w(mag->size);
  for (int i = 0; i < mag->size; ++i) {
    w[i] = static_cast<uint64_t>(mag->base()[i]);
  }
  // Repeatedly divide the whole magnitude by 1e9, collecting 9-digit groups.
  constexpr uint64_t      base1e9 = 1000000000ULL;
  std::vector<uint32_t>   groups;  // little-endian (least-significant first)
  bool                    nonzero = true;
  while (nonzero) {
    unsigned __int128 rem = 0;
    for (int i = static_cast<int>(w.size()) - 1; i >= 0; --i) {
      unsigned __int128 cur = (rem << 64) | w[i];
      w[i]                  = static_cast<uint64_t>(cur / base1e9);
      rem                   = cur % base1e9;
    }
    groups.push_back(static_cast<uint32_t>(rem));
    nonzero = false;
    for (uint64_t x : w) {
      if (x != 0) {
        nonzero = true;
        break;
      }
    }
  }
  // Most-significant group unpadded; interior groups zero-padded to 9 digits.
  std::string out = std::to_string(groups.back());
  for (int i = static_cast<int>(groups.size()) - 2; i >= 0; --i) {
    std::string g = std::to_string(groups[i]);
    out += std::string(9 - g.size(), '0');
    out += g;
  }
  return neg ? "-" + out : out;
}

// Arbitrary-precision hex ("0x.." / "-0x.."). Mirrors to_pyrope's hex paths but
// always hex (no decimal-for-small shortcut). Non-plain-integer → to_pyrope.
std::string Dlop::to_hex_string() const {
  if (is_invalid() || type == Type::String || has_unknowns()) {
    return to_pyrope();
  }
  std::string     result;
  const Dlop*     mag = this;
  spool_ptr<Dlop> holder;
  if (is_negative()) {
    result = "-0x";
    holder = neg_op();
    mag    = holder.get();
  } else {
    result = "0x";
  }
  for (int i = mag->size - 1; i >= 0; --i) {
    if (i == mag->size - 1) {
      result += std::format("{:x}", static_cast<uint64_t>(mag->base()[i]));
    } else {
      result += std::format("{:016x}", static_cast<uint64_t>(mag->base()[i]));
    }
  }
  return result;
}

std::string Dlop::to_verilog() const {
  if (is_known_false()) {
    return "'sb0";
  }

  if (has_unknowns()) {
    auto bin = to_binary();
    return std::format("{}'sb{}", get_bits(), bin);
  }

  if (type == Type::String) {
    return std::format("\"{}\"", to_string());
  }

  int nbits = get_bits();

  // For negatives, format the two's-complement magnitude (neg_op) as hex.
  // Don't route through to_pyrope().substr(2): to_pyrope returns decimal
  // (no "0x" prefix) for small values in [-63,63], so the substr(2) would
  // throw std::out_of_range on e.g. -1 -> neg_op() == 1 -> "1".
  const Dlop*     src = this;
  spool_ptr<Dlop> pos;
  if (is_negative()) {
    pos = neg_op();
    src = pos.get();
  }

  if (src->is_just_i64()) {
    return std::format("{}'sh{:x}", nbits, static_cast<uint64_t>(src->base()[0]));
  }

  std::string hex;
  for (int i = src->size - 1; i >= 0; --i) {
    if (i == src->size - 1) {
      hex += std::format("{:x}", static_cast<uint64_t>(src->base()[i]));
    } else {
      hex += std::format("{:016x}", static_cast<uint64_t>(src->base()[i]));
    }
  }
  return std::format("{}'sh{}", nbits, hex);
}

// =========================================================================
// Debug
// =========================================================================
void Dlop::dump() const {
  std::print("size:{}\n  base:0x", size);
  for (int i = size - 1; i >= 0; --i) {
    std::print("_{:016x}", (uint64_t)base()[i]);
  }
  std::print("\n extra:0x");
  for (int i = size - 1; i >= 0; --i) {
    std::print("_{:016x}", (uint64_t)extra()[i]);
  }
  std::print("\n");
}

// =========================================================================
// Nil — Pyrope tagged unit
// =========================================================================
spool_ptr<Dlop> Dlop::nil() {
  auto dlop = spool_ptr<Dlop>::make();
  dlop->init_nil();
  return dlop;
}

// =========================================================================
// Mask helpers (statics)
// =========================================================================
spool_ptr<Dlop> Dlop::get_mask_value(int bits) {
  if (bits <= 0) {
    return create_integer(1);
  }
  if (bits < 63) {
    return create_integer((int64_t(1) << bits) - 1);
  }
  // Multi-word: lower `bits` ones, sign-extend zero
  int  words      = (bits + 63) / 64 + 1;  // +1 to keep the sign bit clear
  auto d          = spool_ptr<Dlop>::make(Type::Integer, words);
  int  full_words = bits / 64;
  for (int i = 0; i < full_words; ++i) {
    d->base()[i] = -1;
  }
  int leftover = bits % 64;
  if (leftover > 0 && full_words < words) {
    d->base()[full_words] = (int64_t(1) << leftover) - 1;
  }
  d->normalize();
  return d;
}

spool_ptr<Dlop> Dlop::get_mask_value(int h, int l) {
  if (h == l) {
    // Single-bit set at position h
    int  words      = h / 64 + 1 + 1;
    auto d          = spool_ptr<Dlop>::make(Type::Integer, words);
    int  word       = h / 64;
    int  bit        = h % 64;
    d->base()[word] = int64_t(1) << bit;
    d->normalize();
    return d;
  }
  assert(h > l);

  // Bits [l..h] inclusive set; equivalent to ((1<<(h-l+1))-1) << l
  auto width = get_mask_value(h - l + 1);
  return width->shl_op(l);
}

spool_ptr<Dlop> Dlop::get_neg_mask_value(int bits) {
  if (bits <= 1) {
    return create_integer(1);
  }
  // -1 << bits : sign-extended ones in high positions, zeros in low `bits`
  auto neg_one = create_integer(-1);
  return neg_one->shl_op(bits);
}

// =========================================================================
// Instance mask value
// =========================================================================
spool_ptr<Dlop> Dlop::get_mask_value() const {
  if (size == 0) {
    return create_integer(1);
  }
  bool all_zero = true;
  for (int i = 0; i < size; ++i) {
    if (base()[i] != 0) {
      all_zero = false;
      break;
    }
  }
  if (all_zero) {
    return create_integer(1);
  }
  return get_mask_value(get_bits() - 1);
}

// =========================================================================
// Mask range helpers
// Walk contiguous 1-runs in `base`. Negative numbers are treated as their
// bit_flip companion (matches the prior Lconst behavior with Bits_max sentinel).
// =========================================================================
std::vector<std::pair<int, int>> Dlop::get_mask_range_pairs() const {
  std::vector<std::pair<int, int>> pairs;
  if (size == 0) {
    return pairs;
  }
  // All-zero short circuit
  bool all_zero = true;
  for (int i = 0; i < size; ++i) {
    if (base()[i] != 0) {
      all_zero = false;
      break;
    }
  }
  if (all_zero) {
    return pairs;
  }

  const bool neg_mask   = is_negative();
  const int  total_bits = size * 64;

  // Build the working bit vector: for negatives, flip the lower get_bits() to
  // mirror Lconst's bit_flip-of-(-num-1).
  std::vector<uint64_t> work(size);
  for (int i = 0; i < size; ++i) {
    work[i] = static_cast<uint64_t>(base()[i]);
  }
  if (neg_mask) {
    int nbits = get_bits();
    for (int b = 0; b < nbits; ++b) {
      work[b / 64] ^= (uint64_t{1} << (b % 64));
    }
    // Higher bits become zero (sign was 1, flipped to 0)
    for (int b = nbits; b < total_bits; ++b) {
      work[b / 64] &= ~(uint64_t{1} << (b % 64));
    }
  }

  auto bit_at = [&](int p) -> bool {
    if (p < 0 || p >= total_bits) {
      return false;
    }
    return (work[p / 64] >> (p % 64)) & 1;
  };

  int p = 0;
  while (p < total_bits) {
    // Skip zeros
    while (p < total_bits && !bit_at(p)) {
      ++p;
    }
    if (p >= total_bits) {
      break;
    }
    int start = p;
    while (p < total_bits && bit_at(p)) {
      ++p;
    }
    int nones = p - start;

    // Check remainder all-zero (final negative-extended run sentinel)
    bool tail_zero = true;
    for (int q = p; q < total_bits; ++q) {
      if (bit_at(q)) {
        tail_zero = false;
        break;
      }
    }
    if (tail_zero && neg_mask) {
      pairs.emplace_back(start, std::numeric_limits<int>::max() / 2);
      break;
    }
    pairs.emplace_back(start, nones);
  }
  return pairs;
}

std::pair<int, int> Dlop::get_mask_range() const {
  if (size == 0) {
    return {-1, -1};
  }
  bool all_zero = true;
  for (int i = 0; i < size; ++i) {
    if (base()[i] != 0) {
      all_zero = false;
      break;
    }
  }
  if (all_zero) {
    return {-1, -1};
  }

  int range_end;
  if (is_positive()) {
    range_end = get_bits() - 1;
  } else {
    range_end = std::numeric_limits<int>::max() / 2;
  }

  if (is_mask()) {
    return {0, range_end};
  }

  int trail = get_trailing_zeroes();
  if (trail == 0) {
    return {-1, -1};
  }

  auto shifted = sra_op(trail);
  if (shifted->is_mask()) {
    return {trail, range_end};
  }
  return {-1, -1};
}

// =========================================================================
// to_field — Pyrope tuple-field stringification
// String values are emitted unquoted; integers in plain decimal.
// =========================================================================
std::string Dlop::to_field() const {
  if (type == Type::String) {
    assert(!has_unknowns());
    return to_string();
  }
  if (is_negative()) {
    auto pos = neg_op();
    return std::string("-") + pos->to_pyrope().substr(pos->to_pyrope().starts_with("0x") ? 2 : 0);
  }
  if (is_just_i64()) {
    return std::to_string(to_just_i64());
  }
  // Large unsigned: hex without "0x"
  auto p = to_pyrope();
  if (p.starts_with("0x")) {
    return p.substr(2);
  }
  return p;
}

// =========================================================================
// to_known_rand — replace unknown bits with random known bits.
// Deterministic per-process via a thread-local Mersenne.
// =========================================================================
spool_ptr<Dlop> Dlop::to_known_rand() const {
  if (!has_unknowns()) {
    // Return a fresh copy with same payload; extra stays zero from reconstruct.
    auto     d  = spool_ptr<Dlop>::make(type, size);
    int64_t* rb = d->base();
    for (int i = 0; i < size; ++i) {
      rb[i] = base()[i];
    }
    return d;
  }

  static thread_local std::mt19937_64 rng{0xC0FFEEULL};

  auto     d  = spool_ptr<Dlop>::make(type, size);
  int64_t* rb = d->base();
  for (int i = 0; i < size; ++i) {
    uint64_t rnd        = rng();
    // Known bits come from base (since base==base|extra(), base already carries
    // the existing known 1s in non-extra positions). For unknown positions
    // (extra=1), substitute random bits.
    uint64_t known_mask = ~static_cast<uint64_t>(extra()[i]);
    rb[i] = static_cast<int64_t>((static_cast<uint64_t>(base()[i]) & known_mask) | (rnd & static_cast<uint64_t>(extra()[i])));
  }
  return d;
}

// =========================================================================
// Serialization — native layout, no boost dependency.
//   [1 B] type (signed, cast from Type)
//   [2 B] size (big-endian)
//   [size * 8 B] base words (little-endian within each 64-bit word)
//   [size * 8 B] extra words
// =========================================================================
std::string Dlop::serialize() const {
  std::string out;
  out.reserve(3 + size * 16);
  out.push_back(static_cast<char>(static_cast<int16_t>(type) & 0xFF));
  out.push_back(static_cast<char>((static_cast<uint16_t>(size) >> 8) & 0xFF));
  out.push_back(static_cast<char>(static_cast<uint16_t>(size) & 0xFF));
  for (int i = 0; i < size; ++i) {
    uint64_t w = static_cast<uint64_t>(base()[i]);
    for (int b = 0; b < 8; ++b) {
      out.push_back(static_cast<char>((w >> (b * 8)) & 0xFF));
    }
  }
  for (int i = 0; i < size; ++i) {
    uint64_t w = static_cast<uint64_t>(extra()[i]);
    for (int b = 0; b < 8; ++b) {
      out.push_back(static_cast<char>((w >> (b * 8)) & 0xFF));
    }
  }
  return out;
}

spool_ptr<Dlop> Dlop::unserialize(std::string_view v) {
  assert(v.size() >= 3);
  int8_t   raw_type = static_cast<int8_t>(v[0]);
  uint16_t sz       = (static_cast<uint8_t>(v[1]) << 8) | static_cast<uint8_t>(v[2]);
  Type     tp       = static_cast<Type>(static_cast<int16_t>(raw_type));

  if (sz == 0) {
    return spool_ptr<Dlop>::make(tp, 0);
  }
  assert(v.size() >= 3u + sz * 16u);

  auto     d   = spool_ptr<Dlop>::make(tp, static_cast<int16_t>(sz));
  int64_t* re  = d->extra_mut();
  int64_t* rb  = d->base();
  size_t   off = 3;
  for (int i = 0; i < sz; ++i) {
    uint64_t w = 0;
    for (int b = 0; b < 8; ++b) {
      w |= static_cast<uint64_t>(static_cast<uint8_t>(v[off + b])) << (b * 8);
    }
    rb[i]  = static_cast<int64_t>(w);
    off   += 8;
  }
  for (int i = 0; i < sz; ++i) {
    uint64_t w = 0;
    for (int b = 0; b < 8; ++b) {
      w |= static_cast<uint64_t>(static_cast<uint8_t>(v[off + b])) << (b * 8);
    }
    re[i]  = static_cast<int64_t>(w);
    off   += 8;
  }
  // A size-1 payload usually fits the inline encodings; reclaim them.
  d->compact_inline();
  return d;
}

// =========================================================================
// hash — 64-bit FNV-1a over (type, size, base words, extra words)
// =========================================================================
uint64_t Dlop::hash() const {
  constexpr uint64_t FNV_OFFSET = 14695981039346656037ULL;
  constexpr uint64_t FNV_PRIME  = 1099511628211ULL;
  uint64_t           h          = FNV_OFFSET;

  auto mix_u64 = [&](uint64_t w) {
    for (int b = 0; b < 8; ++b) {
      h ^= static_cast<uint8_t>(w >> (b * 8));
      h *= FNV_PRIME;
    }
  };

  mix_u64(static_cast<uint64_t>(static_cast<int16_t>(type)));
  mix_u64(static_cast<uint64_t>(size));
  for (int i = 0; i < size; ++i) {
    mix_u64(static_cast<uint64_t>(base()[i]));
  }
  for (int i = 0; i < size; ++i) {
    mix_u64(static_cast<uint64_t>(extra()[i]));
  }
  return h;
}
