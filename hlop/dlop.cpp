//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "dlop.hpp"

#include <cstdlib>
#include <cstring>
#include <format>
#include <print>
#include <random>

#include "likely.hpp"
#include "str_tools.hpp"

// =========================================================================
// Memory management
// =========================================================================
void Dlop::free(size_t sz, int64_t* ptr) {
  // Grow the per-thread pool on demand. `free` can be called for a size whose
  // pool slot was never populated on this thread — e.g. during late teardown
  // of long-lived bundles whose Dlops were allocated through a different
  // execution path. The growth mirrors `alloc` so released buffers always
  // have a home to return to.
  while ((sz >> 3) >= free_pool.size()) {
    auto* p = new raw_ptr_pool((free_pool.size() + 1) << 6);
    free_pool.emplace_back(p);
  }
  free_pool[sz >> 3]->release_ptr(ptr);
}

int64_t* Dlop::alloc(size_t sz) {
  assert(sz >= 1);
  if (likely(free_pool.size() > (sz >> 3))) {
    return static_cast<int64_t*>(free_pool[sz >> 3]->get_ptr());
  }
  while ((sz >> 3) >= free_pool.size()) {
    auto* ptr = new raw_ptr_pool((free_pool.size() + 1) << 6);
    free_pool.emplace_back(ptr);
  }
  return static_cast<int64_t*>(free_pool[sz >> 3]->get_ptr());
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
    // Only reachable from size == 0; layout stays inline.
    data[0] = base_sign;
    data[1] = extra_sign;
    size    = new_size;
    return;
  }

  int64_t* new_base  = alloc(new_size);
  int64_t* new_extra = alloc(new_size);

  for (int i = 0; i < size; ++i) {
    new_base[i]  = base()[i];
    new_extra[i] = extra()[i];
  }
  for (int i = size; i < new_size; ++i) {
    new_base[i]  = base_sign;
    new_extra[i] = extra_sign;
  }

  if (size > 1) {
    free(size, big.bp);
    free(size, big.ep);
  }
  big.bp = new_base;
  big.ep = new_extra;
  size   = new_size;
}

void Dlop::normalize() {
  if (size <= 1) {
    return;
  }

  int     min_size   = 1;
  int64_t base_sign  = big.bp[size - 1] < 0 ? -1 : 0;
  int64_t extra_sign = big.ep[size - 1] < 0 ? -1 : 0;

  for (int i = size - 1; i >= 1; --i) {
    if (big.bp[i] != base_sign || big.ep[i] != extra_sign) {
      min_size = i + 1;
      break;
    }
    // Check if removing this word would change the sign of the one below
    if ((i > 0) && ((big.bp[i - 1] < 0) != (base_sign < 0))) {
      min_size = i + 1;
      break;
    }
  }

  if (min_size >= size) {
    return;
  }

  if (min_size == 1) {
    // Switching from pool-backed `big` to inline `data` — must read old
    // pointer values out before overwriting the union members.
    int64_t  b      = big.bp[0];
    int64_t  e      = big.ep[0];
    int64_t* old_bp = big.bp;
    int64_t* old_ep = big.ep;
    int16_t  old_sz = size;
    size            = 1;
    data[0]         = b;
    data[1]         = e;
    free(old_sz, old_bp);
    free(old_sz, old_ep);
  }
  // For simplicity, don't reallocate for intermediate sizes
}

// =========================================================================
// In-place initializers — fill `*this` directly. Used by create_* / from_*
// wrappers below, and by callers that embed Dlop in another struct.
// =========================================================================
void Dlop::init_bool(bool val) {
  reconstruct(Type::Boolean, 1);
  data[0] = val ? -1 : 0;
  data[1] = 0;
}

void Dlop::init_integer(int64_t val) {
  reconstruct(Type::Integer, 1);
  data[0] = val;
  data[1] = 0;
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
    int64_t mask = (int64_t(1) << nbits) - 1;
    data[0]      = mask;
    data[1]      = mask;
  } else {
    int words = (nbits + 63) / 64;
    grow_to(static_cast<int16_t>(words));
    for (int i = 0; i < words; ++i) {
      base()[i]  = -1;
      extra()[i] = -1;
    }
    int leftover = nbits % 64;
    if (leftover > 0) {
      int64_t mask       = (int64_t(1) << leftover) - 1;
      base()[words - 1]  = mask;
      extra()[words - 1] = mask;
    }
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
  extra()[word] &= ~(int64_t(1) << bit);
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
  auto dlop        = spool_ptr<Dlop>::make(Type::Boolean, 1);
  dlop->base()[0]  = -1;
  dlop->extra()[0] = -1;
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

  if (orig_txt.size() >= (1 + skip_chars) && std::isdigit(orig_txt[skip_chars])) {
    shift_mode = 10;
    if (orig_txt.size() >= (2 + skip_chars) && orig_txt[skip_chars] == '0') {
      ++skip_chars;
      char sel_ch = lower(orig_txt[skip_chars]);
      if (sel_ch == 's') {
        ++skip_chars;
        sel_ch = lower(orig_txt[skip_chars]);
        if (sel_ch != 'b') {
          throw std::runtime_error(std::format("ERROR: {} unknown pyrope encoding only binary can be signed 0sb...\n", orig_txt));
        }
        assert(!unsigned_result);
      } else if (sel_ch == 'u') {
        // Explicit `0u` prefix: unsigned, then a base selector (x/b/d/o)
        // follows. Matches pyrope syntax `0ub10101`, `0ux3F`, …
        ++skip_chars;
        sel_ch          = lower(orig_txt[skip_chars]);
        unsigned_result = true;
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
      } else if (std::isdigit(sel_ch)) {
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

  reconstruct(Type::Integer, 1 + orig_txt.size() / 16);

  if (shift_mode == 10) {
    for (size_t i = skip_chars; i < orig_txt.size(); ++i) {
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

// =========================================================================
// Mutating arithmetic
// =========================================================================
void Dlop::mut_add(const Dlop& other) {
  if (other.size > size) {
    grow_to(other.size);
  }

  if (size == 1 && other.size == 1) {
    base()[0] += other.base()[0];
    extra()[0] |= other.extra()[0];
    base()[0] |= extra()[0];
  } else {
    int64_t* tmp = alloc(size);
    memcpy(tmp, other.base(), other.size * sizeof(int64_t));
    int64_t sign = (other.base()[other.size - 1] < 0) ? -1 : 0;
    for (int i = other.size; i < size; ++i) {
      tmp[i] = sign;
    }
    Blop::addn(base(), size, base(), tmp);
    Blop::orn(base(), size, base(), extra());
    free(size, tmp);
  }
}

void Dlop::mut_add(int64_t other) {
  if (size == 1) {
    base()[0] += other;
    base()[0] |= extra()[0];
  } else {
    int64_t* tmp = alloc(size);
    Blop::extend(tmp, size, other);
    Blop::addn(base(), size, base(), tmp);
    Blop::orn(base(), size, base(), extra());
    free(size, tmp);
  }
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
  int16_t rsz  = std::max(size, other.size);
  auto    dlop = make_result(Type::Integer, rsz);

  if (rsz == 1 && size == 1 && other.size == 1) {
    // Fast path
    uint64_t combined = static_cast<uint64_t>(extra()[0]) | static_cast<uint64_t>(other.extra()[0]);
    dlop->base()[0]   = base()[0] + other.base()[0];
    // hi_fill: all bits at or above the lowest set bit of combined. Zero if
    // combined is zero. Equivalent to `~(combined - 1) & ~0` when combined>0.
    uint64_t hi_fill = 0u - (combined & (0u - combined));
    dlop->extra()[0] = static_cast<int64_t>(hi_fill);
    dlop->base()[0] |= dlop->extra()[0];  // maintain invariant
  } else {
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
      memset(dlop->extra(), 0, rsz * sizeof(int64_t));
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
        memset(dlop->extra(), 0, rsz * sizeof(int64_t));
      } else {
        for (int w = 0; w < rsz; ++w) {
          if (w < lowest_word) {
            dlop->extra()[w] = 0;
          } else if (w == lowest_word) {
            uint64_t lowmask = static_cast<uint64_t>(1) << lowest_bit;
            dlop->extra()[w] = static_cast<int64_t>(0u - lowmask);
          } else {
            dlop->extra()[w] = -1;  // every bit above is unknown
          }
        }
        // Maintain invariant: unknown bits have base = 1.
        for (int w = 0; w < rsz; ++w) {
          dlop->base()[w] |= dlop->extra()[w];
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

  return dlop;
}

spool_ptr<Dlop> Dlop::add_op(int64_t other) const {
  auto dlop = make_result(Type::Integer, size);

  if (size == 1) {
    dlop->base()[0]  = base()[0] + other;
    dlop->extra()[0] = extra()[0];
    dlop->base()[0] |= dlop->extra()[0];
  } else {
    int64_t* tmp = alloc(size);
    Blop::extend(tmp, size, other);
    Blop::addn(dlop->base(), size, base(), tmp);
    memcpy(dlop->extra(), extra(), size * sizeof(int64_t));
    Blop::orn(dlop->base(), size, dlop->base(), dlop->extra());
    free(size, tmp);
  }

  return dlop;
}

spool_ptr<Dlop> Dlop::sub_op(const Dlop& other) const {
  // sub = add(neg(other))
  auto neg_other = other.neg_op();
  return add_op(neg_other);
}

spool_ptr<Dlop> Dlop::sub_op(int64_t other) const { return add_op(-other); }

// Unknown propagation for multiply: bit k of (a*b) depends on every (i,j)
// with i+j ≤ k. The lowest output bit that can be tainted by an unknown is
// `min(lu_a, lu_b)`, the lowest unknown across both operands — for any k
// below that, every contributing a_i, b_j is known. From that bit up, mark
// unknown (the carry chain mirrors add).
spool_ptr<Dlop> Dlop::mult_op(const Dlop& other) const {
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
    memset(dlop->extra(), 0, rsz * sizeof(int64_t));
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
    memset(dlop->extra(), 0, rsz * sizeof(int64_t));
  } else {
    int lu_word = lu / 64;
    int lu_bit  = lu % 64;
    for (int w = 0; w < rsz; ++w) {
      if (w < lu_word) {
        dlop->extra()[w] = 0;
      } else if (w == lu_word) {
        uint64_t lowmask = static_cast<uint64_t>(1) << lu_bit;
        dlop->extra()[w] = static_cast<int64_t>(0u - lowmask);
      } else {
        dlop->extra()[w] = -1;
      }
    }
    for (int w = 0; w < rsz; ++w) {
      dlop->base()[w] |= dlop->extra()[w];
    }
  }

  dlop->normalize();
  return dlop;
}

spool_ptr<Dlop> Dlop::div_op(const Dlop& other) const {
  if (other.is_known_false()) {
    // Division by zero
    if (is_negative()) {
      return unknown_negative(2);
    }
    return unknown_positive(2);
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

  auto dlop = make_result(Type::Integer, size);

  if (size == 1 && other.size == 1) {
    assert(other.base()[0] != 0);
    dlop->base()[0]  = base()[0] / other.base()[0];
    dlop->extra()[0] = 0;
  } else {
    Blop::divn(dlop->base(), size, base(), size, other.base(), other.size);
    memset(dlop->extra(), 0, size * sizeof(int64_t));
  }

  dlop->normalize();
  return dlop;
}

// mod_op: integer remainder. Returns invalid on mod-by-zero (undefined), a
// 1-bit unknown when either operand has unknowns, and the integer remainder
// otherwise. Only the single-word fast path is implemented; multi-word
// callers currently fall through to invalid.
spool_ptr<Dlop> Dlop::mod_op(const Dlop& other) const {
  // Type guards first: a nil/invalid/string operand has zero words and would
  // otherwise be misclassified as "mod by zero" by the is_known_false() check.
  if (is_invalid() || other.is_invalid() || is_nil() || other.is_nil() || is_string() || other.is_string()) {
    return invalid();
  }
  if (has_unknowns() || other.has_unknowns()) {
    return unknown(1);
  }
  if (other.is_known_false()) {
    return invalid();
  }
  if (size == 1 && other.size == 1) {
    return create_integer(base()[0] % other.base()[0]);
  }
  return invalid();
}

// neg = ~x + 1; the "+1" has a carry chain identical to add. So bits at or
// above the lowest unknown become unknown; bits below stay deterministic.
spool_ptr<Dlop> Dlop::neg_op() const {
  auto dlop = make_result(Type::Integer, size);

  if (has_unknowns()) {
    if (size == 1) {
      dlop->base()[0]  = -base()[0];
      uint64_t e       = static_cast<uint64_t>(extra()[0]);
      uint64_t hi      = 0u - (e & (0u - e));
      dlop->extra()[0] = static_cast<int64_t>(hi);
      dlop->base()[0] |= dlop->extra()[0];
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
        memset(dlop->extra(), 0, size * sizeof(int64_t));
      } else {
        for (int w = 0; w < size; ++w) {
          if (w < lu_word) {
            dlop->extra()[w] = 0;
          } else if (w == lu_word) {
            uint64_t lowmask = static_cast<uint64_t>(1) << lu_bit;
            dlop->extra()[w] = static_cast<int64_t>(0u - lowmask);
          } else {
            dlop->extra()[w] = -1;
          }
        }
        for (int w = 0; w < size; ++w) {
          dlop->base()[w] |= dlop->extra()[w];
        }
      }
    }
    return dlop;
  }

  if (size == 1) {
    dlop->base()[0]  = -base()[0];
    dlop->extra()[0] = 0;
  } else {
    Blop::negn(dlop->base(), size, base());
    memset(dlop->extra(), 0, size * sizeof(int64_t));
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
      dlop->base()[0]  = base()[0] | other.base()[0];
      dlop->extra()[0] = 0;
    } else {
      int64_t known1_a      = base()[0] & ~extra()[0];
      int64_t known1_b      = other.base()[0] & ~other.extra()[0];
      int64_t known0_a      = ~base()[0];
      int64_t known0_b      = ~other.base()[0];
      int64_t result_known1 = known1_a | known1_b;
      int64_t result_known0 = known0_a & known0_b;
      dlop->extra()[0]      = ~result_known1 & ~result_known0;
      dlop->base()[0]       = result_known1 | dlop->extra()[0];  // unknown bits have base=1
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
      memset(dlop->extra(), 0, rsz * sizeof(int64_t));
    } else {
      for (int i = 0; i < rsz; ++i) {
        int64_t known1_a      = s1[i] & ~e1[i];
        int64_t known1_b      = s2[i] & ~e2[i];
        int64_t known0_a      = ~s1[i];
        int64_t known0_b      = ~s2[i];
        int64_t result_known1 = known1_a | known1_b;
        int64_t result_known0 = known0_a & known0_b;
        dlop->extra()[i]      = ~result_known1 & ~result_known0;
        dlop->base()[i]       = result_known1 | dlop->extra()[i];
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
      dlop->base()[0]  = base()[0] & other.base()[0];
      dlop->extra()[0] = 0;
    } else {
      int64_t known0_a      = ~base()[0];
      int64_t known0_b      = ~other.base()[0];
      int64_t known1_a      = base()[0] & ~extra()[0];
      int64_t known1_b      = other.base()[0] & ~other.extra()[0];
      int64_t result_known0 = known0_a | known0_b;
      int64_t result_known1 = known1_a & known1_b;
      dlop->extra()[0]      = ~result_known0 & ~result_known1;
      dlop->base()[0]       = result_known1 | dlop->extra()[0];
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
      memset(dlop->extra(), 0, rsz * sizeof(int64_t));
    } else {
      for (int i = 0; i < rsz; ++i) {
        int64_t known0_a      = ~s1[i];
        int64_t known0_b      = ~s2[i];
        int64_t known1_a      = s1[i] & ~e1[i];
        int64_t known1_b      = s2[i] & ~e2[i];
        int64_t result_known0 = known0_a | known0_b;
        int64_t result_known1 = known1_a & known1_b;
        dlop->extra()[i]      = ~result_known0 & ~result_known1;
        dlop->base()[i]       = result_known1 | dlop->extra()[i];
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

  int16_t rsz  = std::max(size, other.size);
  auto    dlop = make_result(Type::Integer, rsz);

  if (rsz == 1 && size == 1 && other.size == 1) {
    dlop->base()[0] = base()[0] ^ other.base()[0];
    if (extra()[0] == 0 && other.extra()[0] == 0) {
      dlop->extra()[0] = 0;
    } else {
      dlop->extra()[0] = extra()[0] | other.extra()[0];
      dlop->base()[0] |= dlop->extra()[0];
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

    Blop::xorn(dlop->base(), rsz, s1, s2);
    if (!has_extra() && !other.has_extra()) {
      memset(dlop->extra(), 0, rsz * sizeof(int64_t));
    } else {
      Blop::orn(dlop->extra(), rsz, e1, e2);
      Blop::orn(dlop->base(), rsz, dlop->base(), dlop->extra());
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

  auto dlop = make_result(Type::Integer, size);

  if (size == 1) {
    dlop->base()[0] = ~base()[0];
    if (extra()[0] == 0) {
      dlop->extra()[0] = 0;
    } else {
      // NOT with unknowns: known bits flip, unknown bits stay unknown
      dlop->extra()[0] = extra()[0];
      dlop->base()[0] |= dlop->extra()[0];
    }
  } else {
    Blop::notn(dlop->base(), size, base());
    if (!has_extra()) {
      memset(dlop->extra(), 0, size * sizeof(int64_t));
    } else {
      memcpy(dlop->extra(), extra(), size * sizeof(int64_t));
      Blop::orn(dlop->base(), size, dlop->base(), dlop->extra());
    }
  }

  return dlop;
}

// =========================================================================
// Shift operations
// =========================================================================
spool_ptr<Dlop> Dlop::lsh_op(int64_t amount) const {
  if (amount == 0) {
    auto dlop = make_result(Type::Integer, size);
    memcpy(dlop->base(), base(), size * sizeof(int64_t));
    memcpy(dlop->extra(), extra(), size * sizeof(int64_t));
    return dlop;
  }

  int     extra_words = (amount + 63) / 64;
  int16_t rsz         = size + extra_words;
  auto    dlop        = make_result(Type::Integer, rsz);

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
    Blop::shln(dlop->extra(), rsz, tmp_extra, amount);
  } else {
    memset(dlop->extra(), 0, rsz * sizeof(int64_t));
  }

  free(rsz, tmp_base);
  free(rsz, tmp_extra);

  dlop->normalize();
  return dlop;
}

spool_ptr<Dlop> Dlop::rsh_op(int64_t amount) const {
  if (amount == 0) {
    auto dlop = make_result(Type::Integer, size);
    memcpy(dlop->base(), base(), size * sizeof(int64_t));
    memcpy(dlop->extra(), extra(), size * sizeof(int64_t));
    return dlop;
  }

  auto dlop = make_result(Type::Integer, size);

  Blop::shrn(dlop->base(), size, base(), amount);
  if (has_extra()) {
    Blop::shrn(dlop->extra(), size, extra(), amount);
  } else {
    memset(dlop->extra(), 0, size * sizeof(int64_t));
  }

  dlop->normalize();
  return dlop;
}

// Dlop-typed shift wrappers: forward to the int64 form once the amount is
// confirmed numeric and known. Unknown-amount widths follow eval.hpp's
// convention — left shift widens conservatively by 64 (could grow), right
// shift keeps the source width (cannot grow). Non-numeric / nil amount is
// invalid.
spool_ptr<Dlop> Dlop::lsh_op(const Dlop& amount) const {
  if (amount.has_unknowns()) {
    return unknown(get_bits() + 64);
  }
  if (!amount.is_i()) {
    return invalid();
  }
  return lsh_op(amount.to_i());
}

spool_ptr<Dlop> Dlop::rsh_op(const Dlop& amount) const {
  if (amount.has_unknowns()) {
    return unknown(get_bits());
  }
  if (!amount.is_i()) {
    return invalid();
  }
  return rsh_op(amount.to_i());
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

  const int64_t b_sign = (size > 0 && base()[size - 1] < 0) ? -1 : 0;
  const int64_t e_sign = (size > 0 && extra()[size - 1] < 0) ? -1 : 0;
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

bool Dlop::cmp_less_known(const Dlop& other) const {
  int16_t rsz = std::max(size, other.size);
  // Signed comparison from MSW down
  int64_t v1_top = (rsz - 1 < size) ? base()[rsz - 1] : (base()[size - 1] < 0 ? -1 : 0);
  int64_t v2_top = (rsz - 1 < other.size) ? other.base()[rsz - 1] : (other.base()[other.size - 1] < 0 ? -1 : 0);
  if (v1_top != v2_top) {
    return v1_top < v2_top;
  }

  for (int i = rsz - 2; i >= 0; --i) {
    uint64_t v1 = (i < size) ? static_cast<uint64_t>(base()[i]) : (base()[size - 1] < 0 ? ~uint64_t(0) : 0);
    uint64_t v2 = (i < other.size) ? static_cast<uint64_t>(other.base()[i]) : (other.base()[other.size - 1] < 0 ? ~uint64_t(0) : 0);
    if (v1 != v2) {
      return v1 < v2;
    }
  }
  return false;
}

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

    int64_t combined_extra = e1 | e2;
    // diff_known: bits where both sides are known AND they disagree.
    int64_t diff_known     = (b1 ^ b2) & ~combined_extra;
    uint64_t interesting   = static_cast<uint64_t>(diff_known | combined_extra);

    while (interesting != 0) {
      int bit = 63 - __builtin_clzll(interesting);  // highest set bit
      uint64_t mask = uint64_t(1) << bit;
      bool unk = (combined_extra & static_cast<int64_t>(mask)) != 0;
      if (unk) {
        return CmpResult::Unknown;
      }
      // Known disagreement at this bit.
      int p = w * 64 + bit;
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
    case CmpResult::Less:    return create_bool(true);
    case CmpResult::Equal:   return create_bool(false);
    case CmpResult::Greater: return create_bool(false);
    case CmpResult::Unknown: return unknown_bool();
  }
  return unknown_bool();
}
spool_ptr<Dlop> Dlop::le_op(const Dlop& other) const {
  auto r = three_way_cmp(*this, other);
  switch (r) {
    case CmpResult::Less:    return create_bool(true);
    case CmpResult::Equal:   return create_bool(true);
    case CmpResult::Greater: return create_bool(false);
    case CmpResult::Unknown: return unknown_bool();
  }
  return unknown_bool();
}
spool_ptr<Dlop> Dlop::gt_op(const Dlop& other) const {
  auto r = three_way_cmp(*this, other);
  switch (r) {
    case CmpResult::Less:    return create_bool(false);
    case CmpResult::Equal:   return create_bool(false);
    case CmpResult::Greater: return create_bool(true);
    case CmpResult::Unknown: return unknown_bool();
  }
  return unknown_bool();
}
spool_ptr<Dlop> Dlop::ge_op(const Dlop& other) const {
  auto r = three_way_cmp(*this, other);
  switch (r) {
    case CmpResult::Less:    return create_bool(false);
    case CmpResult::Equal:   return create_bool(true);
    case CmpResult::Greater: return create_bool(true);
    case CmpResult::Unknown: return unknown_bool();
  }
  return unknown_bool();
}

// =========================================================================
// Bit manipulation
// =========================================================================
spool_ptr<Dlop> Dlop::sext_op(int from_bit) const {
  auto dlop = make_result(Type::Integer, size);

  // Sign-extend both base AND extra from `from_bit`. If the sign bit is
  // unknown (extra bit set) the extension must also be unknown — copying
  // extra verbatim would leave bits above from_bit holding whatever the
  // source had, instead of the sign-extended unknown.
  if (size == 1) {
    Blop::sext64(dlop->base()[0], base()[0], from_bit);
    Blop::sext64(dlop->extra()[0], extra()[0], from_bit);
  } else {
    Blop::sextn(dlop->base(), size, base(), from_bit);
    Blop::sextn(dlop->extra(), size, extra(), from_bit);
  }
  // Maintain invariant: unknown bits have base = 1.
  for (int i = 0; i < size; ++i) {
    dlop->base()[i] |= dlop->extra()[i];
  }

  dlop->normalize();
  return dlop;
}

spool_ptr<Dlop> Dlop::get_mask_op() const {
  // Convert signed value to unsigned mask (absolute value of bits)
  if (!is_negative()) {
    auto dlop = make_result(Type::Integer, size);
    memcpy(dlop->base(), base(), size * sizeof(int64_t));
    memcpy(dlop->extra(), extra(), size * sizeof(int64_t));
    return dlop;
  }

  // Negative: mask = (1 << get_bits()) + value
  int nbits = get_bits();
  int words = (nbits + 63) / 64;
  if (words < 1) {
    words = 1;
  }
  auto dlop = make_result(Type::Integer, words);

  // Compute (1 << nbits) - 1 & value  (clear sign extension)
  if (words == 1) {
    if (nbits < 64) {
      dlop->base()[0] = base()[0] & ((int64_t(1) << nbits) - 1);
    } else {
      dlop->base()[0] = base()[0];
    }
    dlop->extra()[0] = 0;
  } else {
    for (int i = 0; i < std::min(static_cast<int>(size), words); ++i) {
      dlop->base()[i] = base()[i];
    }
    // Mask off the top
    int top_word = (nbits - 1) / 64;
    int top_bit  = nbits % 64;
    if (top_bit > 0 && top_word < words) {
      dlop->base()[top_word] &= (int64_t(1) << top_bit) - 1;
    }
    for (int i = top_word + 1; i < words; ++i) {
      dlop->base()[i] = 0;
    }
    memset(dlop->extra(), 0, words * sizeof(int64_t));
  }

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
  // mask == -1 falls back to the no-arg, sign-strip version.
  if (mask.is_negative() && mask.is_known_true()) {
    bool all_ones = true;
    for (int i = 0; i < mask.size; ++i) {
      if (mask.base()[i] != -1) {
        all_ones = false;
        break;
      }
    }
    if (all_ones) {
      return get_mask_op();
    }
  }

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
  int  end_pos            = mask_neg ? src_bits : std::min(src_bits, positive_mask_bits);

  // Pre-size the output: at most src_bits selected (when all bits pass).
  int  out_words = std::max(1, (src_bits + 63) / 64);
  auto result    = make_result(Type::Integer, static_cast<int16_t>(out_words));
  for (int i = 0; i < out_words; ++i) {
    result->base()[i]  = 0;
    result->extra()[i] = 0;
  }

  // copy_bit: write source bit at position `i` into result at `out_bit`,
  // carrying the unknown flag from extra() so an unknown source bit stays
  // unknown in the output (invariant: unknown bits have base=1).
  auto copy_bit = [&](int i, int out) {
    if (i >= end_pos) {
      return;
    }
    int  sword = i / 64;
    int  sbit  = i % 64;
    int  oword = out / 64;
    int  obit  = out % 64;
    bool b     = (sword < size) ? ((base()[sword] >> sbit) & 1) : false;
    bool u     = (sword < size) ? ((extra()[sword] >> sbit) & 1) : false;
    if (b || u) {
      result->base()[oword] |= int64_t(1) << obit;
    }
    if (u) {
      result->extra()[oword] |= int64_t(1) << obit;
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
  if (mask.is_known_false()) {
    auto dlop = make_result(Type::Integer, size);
    memcpy(dlop->base(), base(), size * sizeof(int64_t));
    memcpy(dlop->extra(), extra(), size * sizeof(int64_t));
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
      memcpy(dlop->base(), value.base(), value.size * sizeof(int64_t));
      memcpy(dlop->extra(), value.extra(), value.size * sizeof(int64_t));
      return dlop;
    }
  }

  assert(!mask.has_unknowns());

  bool mask_neg           = mask.is_negative();
  int  mask_bits          = mask.get_bits();
  int  positive_mask_bits = mask_neg ? (mask_bits - 1) : mask_bits;

  // out_bits: enough to hold base bits and (for negative masks) the value
  // bits past mask_bits.
  int out_bits = std::max(get_bits(), mask_bits);
  if (mask_neg) {
    out_bits = std::max(out_bits, positive_mask_bits + value.get_bits());
  }

  int  out_words = std::max(1, (out_bits + 63) / 64);
  auto result    = make_result(Type::Integer, static_cast<int16_t>(out_words));
  // Start from `this`, sign-extended to out_words. Bits not selected by the
  // mask (including the sign-extension region beyond get_bits()) flow through
  // unchanged; the loop overwrites only the mask-selected positions.
  int64_t base_sign  = (size > 0 && base()[size - 1] < 0) ? -1 : 0;
  int64_t extra_sign = (size > 0 && extra()[size - 1] < 0) ? -1 : 0;
  for (int i = 0; i < out_words; ++i) {
    result->base()[i]  = (i < size) ? base()[i] : base_sign;
    result->extra()[i] = (i < size) ? extra()[i] : extra_sign;
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
      result->base()[w] |= bit_mask;
    } else {
      result->base()[w] &= ~bit_mask;
    }
    if (p.second) {
      result->extra()[w] |= bit_mask;
    } else {
      result->extra()[w] &= ~bit_mask;
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

spool_ptr<Dlop> Dlop::concat_op(const Dlop& other) const {
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
      memcpy(dlop->base(), other.base(), other.size * sizeof(int64_t));
      memcpy(dlop->extra(), other.extra(), other.size * sizeof(int64_t));
      return dlop;
    }
    auto shifted     = other.lsh_op(self_bits);
    auto masked_self = get_mask_op();
    auto r           = shifted->or_op(masked_self);
    r->type          = Type::String;
    return r;
  }

  int other_bits = other.get_bits();
  if (other_bits <= 0) {
    auto dlop = make_result(Type::Integer, size);
    memcpy(dlop->base(), base(), size * sizeof(int64_t));
    memcpy(dlop->extra(), extra(), size * sizeof(int64_t));
    return dlop;
  }

  auto shifted      = lsh_op(other_bits);
  auto masked_other = other.get_mask_op();
  return shifted->or_op(masked_other);
}

spool_ptr<Dlop> Dlop::adjust_bits(int amount) const {
  assert(amount > 0);
  auto dlop = make_result(Type::Integer, size);

  if (size == 1) {
    if (amount < 64) {
      dlop->base()[0] = base()[0] & ((int64_t(1) << amount) - 1);
    } else {
      dlop->base()[0] = base()[0];
    }
    dlop->extra()[0] = extra()[0];
  } else {
    memcpy(dlop->base(), base(), size * sizeof(int64_t));
    memcpy(dlop->extra(), extra(), size * sizeof(int64_t));
    int top_word = amount / 64;
    int top_bit  = amount % 64;
    if (top_word < size && top_bit > 0) {
      dlop->base()[top_word] &= (int64_t(1) << top_bit) - 1;
    }
    for (int i = top_word + 1; i < size; ++i) {
      dlop->base()[i] = 0;
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
  if (base()[top] <= 0) {
    return false;
  }

  // Check top word: must be (2^k - 1)
  if (((base()[top] + 1) & base()[top]) != 0) {
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
  return ((base()[nonzero_idx] - 1) & base()[nonzero_idx]) == 0;
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

bool Dlop::is_i() const {
  if (has_unknowns()) {
    return false;
  }
  return get_bits() <= 62;
}

int64_t Dlop::to_i() const {
  assert(is_i());
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
    return std::format("0b{}", bin);
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

  // Large number: convert to hex
  // For multi-word, output hex from MSW to LSW
  std::string result;
  if (is_negative()) {
    result = "-0x";
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

  if (src->is_i()) {
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
  return width->lsh_op(l);
}

spool_ptr<Dlop> Dlop::get_neg_mask_value(int bits) {
  if (bits <= 1) {
    return create_integer(1);
  }
  // -1 << bits : sign-extended ones in high positions, zeros in low `bits`
  auto neg_one = create_integer(-1);
  return neg_one->lsh_op(bits);
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

  auto shifted = rsh_op(trail);
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
  if (is_i()) {
    return std::to_string(to_i());
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
    // Return a fresh copy with same payload
    auto d = spool_ptr<Dlop>::make(type, size);
    for (int i = 0; i < size; ++i) {
      d->base()[i]  = base()[i];
      d->extra()[i] = 0;
    }
    return d;
  }

  static thread_local std::mt19937_64 rng{0xC0FFEEULL};

  auto d = spool_ptr<Dlop>::make(type, size);
  for (int i = 0; i < size; ++i) {
    uint64_t rnd = rng();
    // Known bits come from base (since base==base|extra(), base already carries
    // the existing known 1s in non-extra positions). For unknown positions
    // (extra=1), substitute random bits.
    uint64_t known_mask = ~static_cast<uint64_t>(extra()[i]);
    d->base()[i]
        = static_cast<int64_t>((static_cast<uint64_t>(base()[i]) & known_mask) | (rnd & static_cast<uint64_t>(extra()[i])));
    d->extra()[i] = 0;
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

  auto   d   = spool_ptr<Dlop>::make(tp, static_cast<int16_t>(sz));
  size_t off = 3;
  for (int i = 0; i < sz; ++i) {
    uint64_t w = 0;
    for (int b = 0; b < 8; ++b) {
      w |= static_cast<uint64_t>(static_cast<uint8_t>(v[off + b])) << (b * 8);
    }
    d->base()[i] = static_cast<int64_t>(w);
    off += 8;
  }
  for (int i = 0; i < sz; ++i) {
    uint64_t w = 0;
    for (int b = 0; b < 8; ++b) {
      w |= static_cast<uint64_t>(static_cast<uint8_t>(v[off + b])) << (b * 8);
    }
    d->extra()[i] = static_cast<int64_t>(w);
    off += 8;
  }
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
