//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "dlop.hpp"

#include <cstdlib>
#include <format>
#include <print>

#include "likely.hpp"
#include "str_tools.hpp"

// =========================================================================
// Memory management
// =========================================================================
void Dlop::free(size_t sz, int64_t *ptr) {
  assert(free_pool.size() > (sz >> 3));
  free_pool[sz >> 3]->release_ptr(ptr);
}

int64_t *Dlop::alloc(size_t sz) {
  assert(sz >= 1);
  if (likely(free_pool.size() > (sz >> 3))) {
    return static_cast<int64_t *>(free_pool[sz >> 3]->get_ptr());
  }
  while ((sz >> 3) >= free_pool.size()) {
    auto *ptr = new raw_ptr_pool((free_pool.size() + 1) << 6);
    free_pool.emplace_back(ptr);
  }
  return static_cast<int64_t *>(free_pool[sz >> 3]->get_ptr());
}

spool_ptr<Dlop> Dlop::make_result(Type tp, int16_t sz) {
  auto dlop = spool_ptr<Dlop>::make(tp, sz);
  return dlop;
}

void Dlop::grow_to(int16_t new_size) {
  if (new_size <= size) return;

  int64_t *new_base  = alloc(new_size);
  int64_t *new_extra = alloc(new_size);

  int64_t base_sign  = base[size - 1] < 0 ? -1 : 0;
  int64_t extra_sign = extra[size - 1] < 0 ? -1 : 0;

  for (int i = 0; i < size; ++i) {
    new_base[i]  = base[i];
    new_extra[i] = extra[i];
  }
  for (int i = size; i < new_size; ++i) {
    new_base[i]  = base_sign;
    new_extra[i] = extra_sign;
  }

  if (size > 1) {
    free(size, base);
    free(size, extra);
  }
  base  = new_base;
  extra = new_extra;
  size  = new_size;
}

void Dlop::normalize() {
  if (size <= 1) return;

  int min_size = 1;
  int64_t base_sign  = base[size - 1] < 0 ? -1 : 0;
  int64_t extra_sign = extra[size - 1] < 0 ? -1 : 0;

  for (int i = size - 1; i >= 1; --i) {
    if (base[i] != base_sign || extra[i] != extra_sign) {
      min_size = i + 1;
      break;
    }
    // Check if removing this word would change the sign of the one below
    if ((i > 0) && ((base[i - 1] < 0) != (base_sign < 0))) {
      min_size = i + 1;
      break;
    }
  }

  if (min_size >= size) return;

  if (min_size == 1) {
    int64_t b = base[0];
    int64_t e = extra[0];
    if (size > 1) {
      free(size, base);
      free(size, extra);
    }
    base    = &data[0];
    extra   = &data[1];
    data[0] = b;
    data[1] = e;
    size    = 1;
  }
  // For simplicity, don't reallocate for intermediate sizes
}

// =========================================================================
// Factory methods
// =========================================================================
spool_ptr<Dlop> Dlop::create_bool(bool val) {
  auto dlop = spool_ptr<Dlop>::make(Type::Boolean, 1);
  dlop->base[0]  = val ? -1 : 0;
  dlop->extra[0] = 0;
  return dlop;
}

spool_ptr<Dlop> Dlop::create_integer(int64_t val) {
  auto dlop = spool_ptr<Dlop>::make(Type::Integer, 1);
  dlop->base[0]  = val;
  dlop->extra[0] = 0;
  return dlop;
}

spool_ptr<Dlop> Dlop::create_string(std::string_view txt) {
  auto dlop = spool_ptr<Dlop>::make(Type::String, 1 + txt.size() / 8);
  for (int i = txt.size() - 1; i >= 0; --i) {
    dlop->shl_base(8);
    dlop->or_base(static_cast<unsigned char>(txt[i]));
  }
  return dlop;
}

spool_ptr<Dlop> Dlop::from_string(std::string_view txt) {
  return create_string(txt);
}

spool_ptr<Dlop> Dlop::invalid() {
  return spool_ptr<Dlop>::make(Type::Invalid, 0);
}

spool_ptr<Dlop> Dlop::unknown(int nbits) {
  // All bits unknown: extra = all 1s for nbits, base = all 1s for nbits
  // Encoded as signed unknown with ? for every bit position
  auto dlop = create_integer(0);
  if (nbits <= 0) return dlop;

  // Set nbits of extra to 1, and corresponding base bits to 1
  if (nbits <= 63) {
    int64_t mask = (int64_t(1) << nbits) - 1;
    dlop->base[0]  = mask;
    dlop->extra[0] = mask;
  } else {
    int words = (nbits + 63) / 64;
    dlop->grow_to(words);
    for (int i = 0; i < words; ++i) {
      dlop->base[i]  = -1;
      dlop->extra[i] = -1;
    }
    int leftover = nbits % 64;
    if (leftover > 0) {
      int64_t mask = (int64_t(1) << leftover) - 1;
      dlop->base[words - 1]  = mask;
      dlop->extra[words - 1] = mask;
    }
  }
  return dlop;
}

spool_ptr<Dlop> Dlop::unknown_positive(int nbits) {
  // Known sign bit = 0, rest unknown
  if (nbits <= 1) return create_integer(0);
  auto dlop = unknown(nbits - 1);
  // Sign bit (position nbits-1) is 0 in base, 0 in extra => known 0
  // Lower bits are already all-unknown from unknown(nbits-1)
  return dlop;
}

spool_ptr<Dlop> Dlop::unknown_negative(int nbits) {
  // Known sign bit = 1, rest unknown
  if (nbits <= 1) return create_integer(-1);
  auto dlop = unknown(nbits);
  // Set sign bit in base to 1 (already done by unknown), clear it in extra
  int word = (nbits - 1) / 64;
  int bit  = (nbits - 1) % 64;
  dlop->extra[word] &= ~(int64_t(1) << bit);
  // The base bit at sign position stays 1 (negative)
  return dlop;
}

// =========================================================================
// from_binary
// =========================================================================
spool_ptr<Dlop> Dlop::from_binary(std::string_view txt, bool unsigned_result) {
  auto dlop = spool_ptr<Dlop>::make(Type::Integer, 1 + txt.size() / 64);
  if (!unsigned_result) {
    for (size_t i = 0; i < txt.size(); ++i) {
      const auto ch2 = txt[i];
      if (ch2 == '_') continue;
      if (ch2 == '1') {
        dlop->extend_base(-1);
      } else if (ch2 == '?') {
        dlop->extend_extra(-1);
      }
      break;
    }
  }

  for (size_t i = 0; i < txt.size(); ++i) {
    const auto ch2 = txt[i];
    if (ch2 == '_') continue;

    dlop->shl_base(1);
    dlop->shl_extra(1);
    if (ch2 == '?' || ch2 == 'x') {
      dlop->or_extra(1);
      dlop->or_base(1);  // unknown bits have base=1 (invariant: base == base|extra)
    } else if (ch2 == 'z') {
      dlop->or_extra(1);
      dlop->or_base(1);
    } else if (ch2 == '0') {
      // nothing
    } else if (ch2 == '1') {
      dlop->or_base(1);
    } else {
      throw std::runtime_error(std::format("ERROR: {} binary encoding could not use {}\n", txt, ch2));
    }
  }

  return dlop;
}

// =========================================================================
// from_pyrope
// =========================================================================
spool_ptr<Dlop> Dlop::from_pyrope(std::string_view orig_txt) {
  if (orig_txt.empty()) {
    return spool_ptr<Dlop>::make(Type::Invalid, 0);
  }

  auto txt = str_tools::to_lower(orig_txt);

  if (txt == "true")  return Dlop::create_bool(true);
  if (txt == "false") return Dlop::create_bool(false);

  bool negative   = false;
  auto skip_chars = 0u;

  if (txt.front() == '-') {
    negative   = true;
    skip_chars = 1;
  } else if (txt.front() == '+') {
    skip_chars = 1;
  }

  auto shift_mode      = 0;
  bool unsigned_result = false;

  if (txt.size() >= (1 + skip_chars) && std::isdigit(txt[skip_chars])) {
    shift_mode = 10;
    if (txt.size() >= (2 + skip_chars) && txt[skip_chars] == '0') {
      ++skip_chars;
      auto sel_ch = txt[skip_chars];
      if (sel_ch == 's') {
        ++skip_chars;
        sel_ch = txt[skip_chars];
        if (sel_ch != 'b') {
          throw std::runtime_error(std::format("ERROR: {} unknown pyrope encoding only binary can be signed 0sb...\n", orig_txt));
        }
        assert(!unsigned_result);
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
    int start_i = static_cast<int>(orig_txt.size());
    int end_i   = 0;

    if (orig_txt.size() > 1 && orig_txt.front() == '\'' && orig_txt.back() == '\'') {
      --start_i;
      ++end_i;
    }

    return Dlop::create_string(orig_txt.substr(end_i, start_i - end_i));
  }

  auto dlop = spool_ptr<Dlop>::make(Type::Integer, 1 + txt.size() / 16);

  if (shift_mode == 10) {
    for (auto i = skip_chars; i < txt.size(); ++i) {
      auto v = char_to_val[(uint8_t)txt[i]];
      if (likely(v >= 0)) {
        dlop->mult_base(10);
        dlop->add_base(v);
      } else {
        if (txt[i] == '_') continue;
        throw std::runtime_error(std::format("ERROR: {} encoding could not use {}\n", orig_txt, txt[i]));
      }
    }
  } else if (shift_mode == 1) {
    auto v = from_binary(txt.substr(skip_chars), unsigned_result);
    if (negative) {
      v->negate_mut();
    }
    return v;
  } else {
    assert(shift_mode == 3 || shift_mode == 4);

    for (auto i = skip_chars; i < txt.size(); ++i) {
      if (txt[i] == '_') continue;

      auto v = char_to_val[(uint8_t)txt[i]];
      if (unlikely(v < 0)) {
        throw std::runtime_error(std::format("ERROR: {} encoding could not use {}\n", orig_txt, txt[i]));
      }

      auto char_sa = char_to_bits[(uint8_t)txt[i]];
      if (unlikely(char_sa > shift_mode)) {
        throw std::runtime_error(
            std::format("ERROR: {} invalid syntax for number {} bits needed for '{}'", orig_txt, char_sa, txt[i]));
      }
      dlop->shl_base(shift_mode);
      dlop->or_base(v);
    }

    assert(unsigned_result);
  }

  if (negative) {
    dlop->negate_mut();
  }

  return dlop;
}

// =========================================================================
// Mutating arithmetic
// =========================================================================
void Dlop::mut_add(spool_ptr<Dlop> other) {
  if (other->size > size) {
    grow_to(other->size);
  }

  if (size == 1 && other->size == 1) {
    base[0] += other->base[0];
    extra[0] |= other->extra[0];
    base[0] |= extra[0];
  } else {
    int64_t *tmp = alloc(size);
    memcpy(tmp, other->base, other->size * sizeof(int64_t));
    int64_t sign = (other->base[other->size - 1] < 0) ? -1 : 0;
    for (int i = other->size; i < size; ++i) {
      tmp[i] = sign;
    }
    Blop::addn(base, size, base, tmp);
    Blop::orn(base, size, base, extra);
    free(size, tmp);
  }
}

void Dlop::mut_add(int64_t other) {
  if (size == 1) {
    base[0] += other;
    base[0] |= extra[0];
  } else {
    int64_t *tmp = alloc(size);
    Blop::extend(tmp, size, other);
    Blop::addn(base, size, base, tmp);
    Blop::orn(base, size, base, extra);
    free(size, tmp);
  }
}

// =========================================================================
// Arithmetic operations
// =========================================================================
spool_ptr<Dlop> Dlop::add_op(spool_ptr<Dlop> other) const {
  int16_t rsz = std::max(size, other->size);
  auto dlop = make_result(Type::Integer, rsz);

  if (rsz == 1 && size == 1 && other->size == 1) {
    // Fast path
    if (extra[0] == 0 && other->extra[0] == 0) {
      dlop->base[0]  = base[0] + other->base[0];
      dlop->extra[0] = 0;
    } else {
      // Unknown propagation for addition (conservative)
      // Result bits where either input has unknowns become unknown
      dlop->base[0]  = base[0] + other->base[0];
      dlop->extra[0] = extra[0] | other->extra[0]; // conservative
      dlop->base[0] |= dlop->extra[0]; // maintain invariant
    }
  } else {
    // Multi-word: sign-extend shorter operand
    int64_t *s1 = base;
    int64_t *s2 = other->base;
    int64_t *e1 = extra;
    int64_t *e2 = other->extra;

    // Use temp arrays for sign extension if needed
    int64_t *s1_ext = nullptr;
    int64_t *s2_ext = nullptr;
    int64_t *e1_ext = nullptr;
    int64_t *e2_ext = nullptr;

    if (size < rsz) {
      s1_ext = alloc(rsz);
      e1_ext = alloc(rsz);
      Blop::extend(s1_ext, rsz, base[size - 1] < 0 ? -1 : 0);
      Blop::extend(e1_ext, rsz, extra[size - 1] < 0 ? -1 : 0);
      for (int i = 0; i < size; ++i) { s1_ext[i] = base[i]; e1_ext[i] = extra[i]; }
      s1 = s1_ext; e1 = e1_ext;
    }
    if (other->size < rsz) {
      s2_ext = alloc(rsz);
      e2_ext = alloc(rsz);
      Blop::extend(s2_ext, rsz, other->base[other->size - 1] < 0 ? -1 : 0);
      Blop::extend(e2_ext, rsz, other->extra[other->size - 1] < 0 ? -1 : 0);
      for (int i = 0; i < other->size; ++i) { s2_ext[i] = other->base[i]; e2_ext[i] = other->extra[i]; }
      s2 = s2_ext; e2 = e2_ext;
    }

    Blop::addn(dlop->base, rsz, s1, s2);

    if (!has_extra() && !other->has_extra()) {
      // No unknowns
      memset(dlop->extra, 0, rsz * sizeof(int64_t));
    } else {
      // Conservative: unknown bits propagate
      Blop::orn(dlop->extra, rsz, e1, e2);
      Blop::orn(dlop->base, rsz, dlop->base, dlop->extra);
    }

    if (s1_ext) { free(rsz, s1_ext); free(rsz, e1_ext); }
    if (s2_ext) { free(rsz, s2_ext); free(rsz, e2_ext); }
  }

  return dlop;
}

spool_ptr<Dlop> Dlop::add_op(int64_t other) const {
  auto dlop = make_result(Type::Integer, size);

  if (size == 1) {
    dlop->base[0]  = base[0] + other;
    dlop->extra[0] = extra[0];
    dlop->base[0] |= dlop->extra[0];
  } else {
    int64_t *tmp = alloc(size);
    Blop::extend(tmp, size, other);
    Blop::addn(dlop->base, size, base, tmp);
    memcpy(dlop->extra, extra, size * sizeof(int64_t));
    Blop::orn(dlop->base, size, dlop->base, dlop->extra);
    free(size, tmp);
  }

  return dlop;
}

spool_ptr<Dlop> Dlop::sub_op(spool_ptr<Dlop> other) const {
  // sub = add(neg(other))
  auto neg_other = other->neg_op();
  return add_op(neg_other);
}

spool_ptr<Dlop> Dlop::sub_op(int64_t other) const {
  return add_op(-other);
}

spool_ptr<Dlop> Dlop::mult_op(spool_ptr<Dlop> other) const {
  if (has_unknowns() || other->has_unknowns()) {
    int nbits = get_bits() + other->get_bits();
    bool neg1 = is_negative();
    bool neg2 = other->is_negative();
    if (neg1 != neg2) {
      return unknown_negative(nbits);
    }
    return unknown_positive(nbits);
  }

  int16_t rsz = size + other->size;
  auto dlop = make_result(Type::Integer, rsz);

  if (size == 1 && other->size == 1) {
    // Fast path: result might need 2 words
    __int128 prod = static_cast<__int128>(base[0]) * other->base[0];
    dlop->base[0] = static_cast<int64_t>(prod);
    if (rsz > 1) {
      dlop->base[1] = static_cast<int64_t>(prod >> 64);
    }
    memset(dlop->extra, 0, rsz * sizeof(int64_t));
  } else {
    Blop::multn(dlop->base, rsz, base, size, other->base, other->size);
    memset(dlop->extra, 0, rsz * sizeof(int64_t));
  }

  dlop->normalize();
  return dlop;
}

spool_ptr<Dlop> Dlop::div_op(spool_ptr<Dlop> other) const {
  if (other->is_known_false()) {
    // Division by zero
    if (is_negative()) return unknown_negative(2);
    return unknown_positive(2);
  }

  if (has_unknowns() || other->has_unknowns()) {
    int b = get_bits();
    if (!other->has_unknowns()) {
      b -= other->get_bits();
      if (b <= 0) return create_integer(0);
    }
    bool neg1 = is_negative();
    bool neg2 = other->is_negative();
    if (neg1 != neg2) return unknown_negative(b);
    return unknown_positive(b);
  }

  auto dlop = make_result(Type::Integer, size);

  if (size == 1 && other->size == 1) {
    assert(other->base[0] != 0);
    dlop->base[0]  = base[0] / other->base[0];
    dlop->extra[0] = 0;
  } else {
    Blop::divn(dlop->base, size, base, size, other->base, other->size);
    memset(dlop->extra, 0, size * sizeof(int64_t));
  }

  dlop->normalize();
  return dlop;
}

spool_ptr<Dlop> Dlop::neg_op() const {
  auto dlop = make_result(Type::Integer, size);

  if (has_unknowns()) {
    // neg with unknowns: ~x + 1, unknown bits stay unknown
    if (size == 1) {
      dlop->base[0]  = -base[0];
      dlop->extra[0] = extra[0];
      dlop->base[0] |= dlop->extra[0];
    } else {
      Blop::negn(dlop->base, size, base);
      memcpy(dlop->extra, extra, size * sizeof(int64_t));
      Blop::orn(dlop->base, size, dlop->base, dlop->extra);
    }
    return dlop;
  }

  if (size == 1) {
    dlop->base[0]  = -base[0];
    dlop->extra[0] = 0;
  } else {
    Blop::negn(dlop->base, size, base);
    memset(dlop->extra, 0, size * sizeof(int64_t));
  }

  return dlop;
}

// =========================================================================
// Bitwise operations
// =========================================================================
spool_ptr<Dlop> Dlop::or_op(spool_ptr<Dlop> other) const {
  int16_t rsz = std::max(size, other->size);
  auto dlop = make_result(Type::Integer, rsz);

  // For OR with unknowns (base/extra encoding where unknown bits have base=1):
  //   known_1 = base & ~extra  (definitely 1)
  //   known_0 = ~base          (definitely 0, since unknown has base=1)
  //   A known 1 in either input makes result known 1
  //   Both known 0 makes result known 0
  //   Otherwise unknown

  if (rsz == 1 && size == 1 && other->size == 1) {
    if (extra[0] == 0 && other->extra[0] == 0) {
      dlop->base[0] = base[0] | other->base[0];
      dlop->extra[0] = 0;
    } else {
      int64_t known1_a = base[0] & ~extra[0];
      int64_t known1_b = other->base[0] & ~other->extra[0];
      int64_t known0_a = ~base[0];
      int64_t known0_b = ~other->base[0];
      int64_t result_known1 = known1_a | known1_b;
      int64_t result_known0 = known0_a & known0_b;
      dlop->extra[0] = ~result_known1 & ~result_known0;
      dlop->base[0]  = result_known1 | dlop->extra[0];  // unknown bits have base=1
    }
  } else {
    // Sign-extend shorter operand
    int64_t *s1 = base, *s2 = other->base;
    int64_t *e1 = extra, *e2 = other->extra;
    int64_t *s1_ext = nullptr, *s2_ext = nullptr, *e1_ext = nullptr, *e2_ext = nullptr;

    if (size < rsz) {
      s1_ext = alloc(rsz); e1_ext = alloc(rsz);
      Blop::extend(s1_ext, rsz, base[size - 1] < 0 ? -1 : 0);
      Blop::extend(e1_ext, rsz, extra[size - 1] < 0 ? -1 : 0);
      for (int i = 0; i < size; ++i) { s1_ext[i] = base[i]; e1_ext[i] = extra[i]; }
      s1 = s1_ext; e1 = e1_ext;
    }
    if (other->size < rsz) {
      s2_ext = alloc(rsz); e2_ext = alloc(rsz);
      Blop::extend(s2_ext, rsz, other->base[other->size - 1] < 0 ? -1 : 0);
      Blop::extend(e2_ext, rsz, other->extra[other->size - 1] < 0 ? -1 : 0);
      for (int i = 0; i < other->size; ++i) { s2_ext[i] = other->base[i]; e2_ext[i] = other->extra[i]; }
      s2 = s2_ext; e2 = e2_ext;
    }

    if (!has_extra() && !other->has_extra()) {
      Blop::orn(dlop->base, rsz, s1, s2);
      memset(dlop->extra, 0, rsz * sizeof(int64_t));
    } else {
      for (int i = 0; i < rsz; ++i) {
        int64_t known1_a = s1[i] & ~e1[i];
        int64_t known1_b = s2[i] & ~e2[i];
        int64_t known0_a = ~s1[i];
        int64_t known0_b = ~s2[i];
        int64_t result_known1 = known1_a | known1_b;
        int64_t result_known0 = known0_a & known0_b;
        dlop->extra[i] = ~result_known1 & ~result_known0;
        dlop->base[i]  = result_known1 | dlop->extra[i];
      }
    }

    if (s1_ext) { free(rsz, s1_ext); free(rsz, e1_ext); }
    if (s2_ext) { free(rsz, s2_ext); free(rsz, e2_ext); }
  }

  return dlop;
}

spool_ptr<Dlop> Dlop::and_op(spool_ptr<Dlop> other) const {
  int16_t rsz = std::max(size, other->size);
  auto dlop = make_result(Type::Integer, rsz);

  // For AND with unknowns (base/extra encoding where unknown bits have base=1):
  //   known_0 = ~base          (definitely 0)
  //   known_1 = base & ~extra  (definitely 1)
  //   A known 0 in either input makes result known 0
  //   Both known 1 makes result known 1
  //   Otherwise unknown

  if (rsz == 1 && size == 1 && other->size == 1) {
    if (extra[0] == 0 && other->extra[0] == 0) {
      dlop->base[0] = base[0] & other->base[0];
      dlop->extra[0] = 0;
    } else {
      int64_t known0_a = ~base[0];
      int64_t known0_b = ~other->base[0];
      int64_t known1_a = base[0] & ~extra[0];
      int64_t known1_b = other->base[0] & ~other->extra[0];
      int64_t result_known0 = known0_a | known0_b;
      int64_t result_known1 = known1_a & known1_b;
      dlop->extra[0] = ~result_known0 & ~result_known1;
      dlop->base[0]  = result_known1 | dlop->extra[0];
    }
  } else {
    int64_t *s1 = base, *s2 = other->base;
    int64_t *e1 = extra, *e2 = other->extra;
    int64_t *s1_ext = nullptr, *s2_ext = nullptr, *e1_ext = nullptr, *e2_ext = nullptr;

    if (size < rsz) {
      s1_ext = alloc(rsz); e1_ext = alloc(rsz);
      Blop::extend(s1_ext, rsz, base[size - 1] < 0 ? -1 : 0);
      Blop::extend(e1_ext, rsz, extra[size - 1] < 0 ? -1 : 0);
      for (int i = 0; i < size; ++i) { s1_ext[i] = base[i]; e1_ext[i] = extra[i]; }
      s1 = s1_ext; e1 = e1_ext;
    }
    if (other->size < rsz) {
      s2_ext = alloc(rsz); e2_ext = alloc(rsz);
      Blop::extend(s2_ext, rsz, other->base[other->size - 1] < 0 ? -1 : 0);
      Blop::extend(e2_ext, rsz, other->extra[other->size - 1] < 0 ? -1 : 0);
      for (int i = 0; i < other->size; ++i) { s2_ext[i] = other->base[i]; e2_ext[i] = other->extra[i]; }
      s2 = s2_ext; e2 = e2_ext;
    }

    if (!has_extra() && !other->has_extra()) {
      Blop::andn(dlop->base, rsz, s1, s2);
      memset(dlop->extra, 0, rsz * sizeof(int64_t));
    } else {
      for (int i = 0; i < rsz; ++i) {
        int64_t known0_a = ~s1[i];
        int64_t known0_b = ~s2[i];
        int64_t known1_a = s1[i] & ~e1[i];
        int64_t known1_b = s2[i] & ~e2[i];
        int64_t result_known0 = known0_a | known0_b;
        int64_t result_known1 = known1_a & known1_b;
        dlop->extra[i] = ~result_known0 & ~result_known1;
        dlop->base[i]  = result_known1 | dlop->extra[i];
      }
    }

    if (s1_ext) { free(rsz, s1_ext); free(rsz, e1_ext); }
    if (s2_ext) { free(rsz, s2_ext); free(rsz, e2_ext); }
  }

  return dlop;
}

spool_ptr<Dlop> Dlop::xor_op(spool_ptr<Dlop> other) const {
  int16_t rsz = std::max(size, other->size);
  auto dlop = make_result(Type::Integer, rsz);

  if (rsz == 1 && size == 1 && other->size == 1) {
    dlop->base[0] = base[0] ^ other->base[0];
    if (extra[0] == 0 && other->extra[0] == 0) {
      dlop->extra[0] = 0;
    } else {
      dlop->extra[0] = extra[0] | other->extra[0];
      dlop->base[0] |= dlop->extra[0];
    }
  } else {
    int64_t *s1 = base, *s2 = other->base;
    int64_t *e1 = extra, *e2 = other->extra;
    int64_t *s1_ext = nullptr, *s2_ext = nullptr, *e1_ext = nullptr, *e2_ext = nullptr;

    if (size < rsz) {
      s1_ext = alloc(rsz); e1_ext = alloc(rsz);
      Blop::extend(s1_ext, rsz, base[size - 1] < 0 ? -1 : 0);
      Blop::extend(e1_ext, rsz, extra[size - 1] < 0 ? -1 : 0);
      for (int i = 0; i < size; ++i) { s1_ext[i] = base[i]; e1_ext[i] = extra[i]; }
      s1 = s1_ext; e1 = e1_ext;
    }
    if (other->size < rsz) {
      s2_ext = alloc(rsz); e2_ext = alloc(rsz);
      Blop::extend(s2_ext, rsz, other->base[other->size - 1] < 0 ? -1 : 0);
      Blop::extend(e2_ext, rsz, other->extra[other->size - 1] < 0 ? -1 : 0);
      for (int i = 0; i < other->size; ++i) { s2_ext[i] = other->base[i]; e2_ext[i] = other->extra[i]; }
      s2 = s2_ext; e2 = e2_ext;
    }

    Blop::xorn(dlop->base, rsz, s1, s2);
    if (!has_extra() && !other->has_extra()) {
      memset(dlop->extra, 0, rsz * sizeof(int64_t));
    } else {
      Blop::orn(dlop->extra, rsz, e1, e2);
      Blop::orn(dlop->base, rsz, dlop->base, dlop->extra);
    }

    if (s1_ext) { free(rsz, s1_ext); free(rsz, e1_ext); }
    if (s2_ext) { free(rsz, s2_ext); free(rsz, e2_ext); }
  }

  return dlop;
}

spool_ptr<Dlop> Dlop::not_op() const {
  auto dlop = make_result(Type::Integer, size);

  if (size == 1) {
    dlop->base[0] = ~base[0];
    if (extra[0] == 0) {
      dlop->extra[0] = 0;
    } else {
      // NOT with unknowns: known bits flip, unknown bits stay unknown
      dlop->extra[0] = extra[0];
      dlop->base[0] |= dlop->extra[0];
    }
  } else {
    Blop::notn(dlop->base, size, base);
    if (!has_extra()) {
      memset(dlop->extra, 0, size * sizeof(int64_t));
    } else {
      memcpy(dlop->extra, extra, size * sizeof(int64_t));
      Blop::orn(dlop->base, size, dlop->base, dlop->extra);
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
    memcpy(dlop->base, base, size * sizeof(int64_t));
    memcpy(dlop->extra, extra, size * sizeof(int64_t));
    return dlop;
  }

  int extra_words = (amount + 63) / 64;
  int16_t rsz = size + extra_words;
  auto dlop = make_result(Type::Integer, rsz);

  // Sign-extend source into larger buffer, then shift
  int64_t *tmp_base = alloc(rsz);
  int64_t *tmp_extra = alloc(rsz);
  Blop::extend(tmp_base, rsz, base[size - 1] < 0 ? -1 : 0);
  Blop::extend(tmp_extra, rsz, extra[size - 1] < 0 ? -1 : 0);
  for (int i = 0; i < size; ++i) {
    tmp_base[i] = base[i];
    tmp_extra[i] = extra[i];
  }

  Blop::shln(dlop->base, rsz, tmp_base, amount);
  if (has_extra()) {
    Blop::shln(dlop->extra, rsz, tmp_extra, amount);
  } else {
    memset(dlop->extra, 0, rsz * sizeof(int64_t));
  }

  free(rsz, tmp_base);
  free(rsz, tmp_extra);

  dlop->normalize();
  return dlop;
}

spool_ptr<Dlop> Dlop::rsh_op(int64_t amount) const {
  if (amount == 0) {
    auto dlop = make_result(Type::Integer, size);
    memcpy(dlop->base, base, size * sizeof(int64_t));
    memcpy(dlop->extra, extra, size * sizeof(int64_t));
    return dlop;
  }

  auto dlop = make_result(Type::Integer, size);

  Blop::shrn(dlop->base, size, base, amount);
  if (has_extra()) {
    Blop::shrn(dlop->extra, size, extra, amount);
  } else {
    memset(dlop->extra, 0, size * sizeof(int64_t));
  }

  dlop->normalize();
  return dlop;
}

// =========================================================================
// Comparison operations
// =========================================================================
spool_ptr<Dlop> Dlop::eq_op(spool_ptr<Dlop> other) const {
  if (has_unknowns() || other->has_unknowns()) {
    return unknown(1);
  }

  if (size == 1 && other->size == 1) {
    return create_bool(base[0] == other->base[0]);
  }

  // Compare with sign extension
  int16_t rsz = std::max(size, other->size);
  for (int i = 0; i < rsz; ++i) {
    int64_t v1 = (i < size) ? base[i] : (base[size - 1] < 0 ? -1 : 0);
    int64_t v2 = (i < other->size) ? other->base[i] : (other->base[other->size - 1] < 0 ? -1 : 0);
    if (v1 != v2) return create_bool(false);
  }
  return create_bool(true);
}

bool Dlop::operator==(const Dlop &other) const {
  if (has_unknowns() || other.has_extra()) return false;
  int16_t rsz = std::max(size, other.size);
  for (int i = 0; i < rsz; ++i) {
    int64_t v1 = (i < size) ? base[i] : (base[size - 1] < 0 ? -1 : 0);
    int64_t v2 = (i < other.size) ? other.base[i] : (other.base[other.size - 1] < 0 ? -1 : 0);
    if (v1 != v2) return false;
  }
  return true;
}

bool Dlop::operator!=(const Dlop &other) const { return !(*this == other); }

bool Dlop::operator<(const Dlop &other) const {
  int16_t rsz = std::max(size, other.size);
  // Signed comparison from MSW down
  int64_t v1_top = (rsz - 1 < size) ? base[rsz - 1] : (base[size - 1] < 0 ? -1 : 0);
  int64_t v2_top = (rsz - 1 < other.size) ? other.base[rsz - 1] : (other.base[other.size - 1] < 0 ? -1 : 0);
  if (v1_top != v2_top) return v1_top < v2_top;

  for (int i = rsz - 2; i >= 0; --i) {
    uint64_t v1 = (i < size) ? static_cast<uint64_t>(base[i]) : (base[size - 1] < 0 ? ~uint64_t(0) : 0);
    uint64_t v2 = (i < other.size) ? static_cast<uint64_t>(other.base[i]) : (other.base[other.size - 1] < 0 ? ~uint64_t(0) : 0);
    if (v1 != v2) return v1 < v2;
  }
  return false;
}

bool Dlop::operator<=(const Dlop &other) const { return !(other < *this); }
bool Dlop::operator>(const Dlop &other) const  { return other < *this; }
bool Dlop::operator>=(const Dlop &other) const { return !(*this < other); }

// =========================================================================
// Bit manipulation
// =========================================================================
spool_ptr<Dlop> Dlop::sext_op(int from_bit) const {
  auto dlop = make_result(Type::Integer, size);
  memcpy(dlop->extra, extra, size * sizeof(int64_t));

  if (size == 1) {
    Blop::sext64(dlop->base[0], base[0], from_bit);
  } else {
    Blop::sextn(dlop->base, size, base, from_bit);
  }

  dlop->normalize();
  return dlop;
}

spool_ptr<Dlop> Dlop::get_mask_op() const {
  // Convert signed value to unsigned mask (absolute value of bits)
  if (!is_negative()) {
    auto dlop = make_result(Type::Integer, size);
    memcpy(dlop->base, base, size * sizeof(int64_t));
    memcpy(dlop->extra, extra, size * sizeof(int64_t));
    return dlop;
  }

  // Negative: mask = (1 << get_bits()) + value
  int nbits = get_bits();
  int words = (nbits + 63) / 64;
  if (words < 1) words = 1;
  auto dlop = make_result(Type::Integer, words);

  // Compute (1 << nbits) - 1 & value  (clear sign extension)
  if (words == 1) {
    if (nbits < 64) {
      dlop->base[0] = base[0] & ((int64_t(1) << nbits) - 1);
    } else {
      dlop->base[0] = base[0];
    }
    dlop->extra[0] = 0;
  } else {
    for (int i = 0; i < std::min(static_cast<int>(size), words); ++i) {
      dlop->base[i] = base[i];
    }
    // Mask off the top
    int top_word = (nbits - 1) / 64;
    int top_bit  = nbits % 64;
    if (top_bit > 0 && top_word < words) {
      dlop->base[top_word] &= (int64_t(1) << top_bit) - 1;
    }
    for (int i = top_word + 1; i < words; ++i) {
      dlop->base[i] = 0;
    }
    memset(dlop->extra, 0, words * sizeof(int64_t));
  }

  return dlop;
}

spool_ptr<Dlop> Dlop::concat_op(spool_ptr<Dlop> other) const {
  int other_bits = other->get_bits();
  if (other_bits <= 0) {
    auto dlop = make_result(Type::Integer, size);
    memcpy(dlop->base, base, size * sizeof(int64_t));
    memcpy(dlop->extra, extra, size * sizeof(int64_t));
    return dlop;
  }

  auto shifted = lsh_op(other_bits);
  auto masked_other = other->get_mask_op();
  return shifted->or_op(masked_other);
}

spool_ptr<Dlop> Dlop::adjust_bits(int amount) const {
  assert(amount > 0);
  auto dlop = make_result(Type::Integer, size);

  if (size == 1) {
    if (amount < 64) {
      dlop->base[0] = base[0] & ((int64_t(1) << amount) - 1);
    } else {
      dlop->base[0] = base[0];
    }
    dlop->extra[0] = extra[0];
  } else {
    memcpy(dlop->base, base, size * sizeof(int64_t));
    memcpy(dlop->extra, extra, size * sizeof(int64_t));
    int top_word = amount / 64;
    int top_bit  = amount % 64;
    if (top_word < size && top_bit > 0) {
      dlop->base[top_word] &= (int64_t(1) << top_bit) - 1;
    }
    for (int i = top_word + 1; i < size; ++i) {
      dlop->base[i] = 0;
    }
  }

  dlop->normalize();
  return dlop;
}

// =========================================================================
// Queries
// =========================================================================
bool Dlop::is_negative() const {
  if (size <= 0) return false;
  return base[size - 1] < 0;
}

bool Dlop::is_positive() const {
  if (size <= 0) return false;
  return base[size - 1] >= 0;
}

bool Dlop::is_known_false() const {
  if (has_unknowns()) return false;
  for (int i = 0; i < size; ++i) {
    if (base[i] != 0) return false;
  }
  return true;
}

bool Dlop::is_known_true() const {
  if (has_unknowns()) {
    // If any known bit is 1, it's true
    for (int i = 0; i < size; ++i) {
      if ((base[i] & ~extra[i]) != 0) return true;
    }
    return false;
  }
  for (int i = 0; i < size; ++i) {
    if (base[i] != 0) return true;
  }
  return false;
}

bool Dlop::is_mask() const {
  if (has_unknowns() || is_negative()) return false;
  // Check if value is (2^n - 1) for some n: all lower bits 1, rest 0
  // Equivalent to: (v + 1) & v == 0 and v != 0
  if (size == 1) {
    return base[0] > 0 && ((base[0] + 1) & base[0]) == 0;
  }
  // Multi-word: add 1 and check if result & original == 0
  // For simplicity, use the bit pattern check
  int top = size - 1;
  while (top > 0 && base[top] == 0) --top;
  if (base[top] <= 0) return false;

  // Check top word: must be (2^k - 1)
  if (((base[top] + 1) & base[top]) != 0) return false;
  // All lower words must be all-ones
  for (int i = 0; i < top; ++i) {
    if (base[i] != -1) return false;
  }
  return true;
}

bool Dlop::is_power2() const {
  if (has_unknowns() || is_negative()) return false;
  if (size == 1) {
    return base[0] > 0 && ((base[0] - 1) & base[0]) == 0;
  }
  // Exactly one bit set
  int nonzero_count = 0;
  int nonzero_idx = -1;
  for (int i = 0; i < size; ++i) {
    if (base[i] != 0) {
      ++nonzero_count;
      nonzero_idx = i;
    }
  }
  if (nonzero_count != 1) return false;
  return ((base[nonzero_idx] - 1) & base[nonzero_idx]) == 0;
}

int Dlop::get_bits() const {
  if (size <= 0) return 0;
  if (size == 1) return Blop::get_bits64(base[0]);
  return Blop::get_bitsn(base, size);
}

bool Dlop::bit_test(int pos) const {
  int word = pos / 64;
  int bit  = pos % 64;
  if (word >= size) {
    return base[size - 1] < 0;  // sign extension
  }
  return (base[word] >> bit) & 1;
}

int Dlop::get_first_bit_set() const {
  for (int i = 0; i < size; ++i) {
    if (base[i] != 0) {
      return i * 64 + __builtin_ctzll(static_cast<uint64_t>(base[i]));
    }
  }
  return -1;
}

int Dlop::get_last_bit_set() const {
  for (int i = size - 1; i >= 0; --i) {
    if (static_cast<uint64_t>(base[i]) != 0) {
      return i * 64 + 63 - __builtin_clzll(static_cast<uint64_t>(base[i]));
    }
  }
  return -1;
}

int Dlop::popcount() const {
  int count = 0;
  for (int i = 0; i < size; ++i) {
    count += __builtin_popcountll(static_cast<uint64_t>(base[i]));
  }
  return count;
}

int Dlop::get_trailing_zeroes() const {
  if (is_known_false()) return 0;
  for (int i = 0; i < size; ++i) {
    if (base[i] != 0) {
      return i * 64 + __builtin_ctzll(static_cast<uint64_t>(base[i]));
    }
  }
  return 0;
}

bool Dlop::is_i() const {
  if (has_unknowns()) return false;
  return get_bits() <= 62;
}

int64_t Dlop::to_i() const {
  assert(is_i());
  return base[0];
}

// =========================================================================
// Conversion to string formats
// =========================================================================
std::string Dlop::to_string() const {
  // Convert base to string (for String type)
  std::string str;
  if (size == 1) {
    uint64_t tmp = static_cast<uint64_t>(base[0]);
    while (tmp) {
      str.push_back(static_cast<char>(tmp & 0xFF));
      tmp >>= 8;
    }
  } else {
    for (int w = 0; w < size; ++w) {
      uint64_t tmp = static_cast<uint64_t>(base[w]);
      for (int b = 0; b < 8; ++b) {
        auto ch = static_cast<char>(tmp & 0xFF);
        if (ch == 0 && w == size - 1) break;
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
    if (nbits <= 0) nbits = 1;
    std::string result;
    for (int i = nbits - 1; i >= 0; --i) {
      bool b = bit_test(i);
      int word = i / 64;
      int bit = i % 64;
      bool is_unknown = (word < size) ? ((extra[word] >> bit) & 1) : false;
      if (is_unknown) {
        result.push_back('?');
      } else {
        result.push_back(b ? '1' : '0');
      }
    }
    return result;
  }

  int nbits = get_bits();
  if (nbits <= 0) return "0";

  std::string result;
  for (int i = nbits - 1; i >= 0; --i) {
    result.push_back(bit_test(i) ? '1' : '0');
  }
  return result;
}

std::string Dlop::to_pyrope() const {
  if (is_invalid()) return "";

  if (type == Type::String) {
    auto str = to_string();
    if (str.empty()) return "''";
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
        result += std::format("{:x}", static_cast<uint64_t>(pos->base[i]));
      } else {
        result += std::format("{:016x}", static_cast<uint64_t>(pos->base[i]));
      }
    }
  } else {
    result = "0x";
    for (int i = size - 1; i >= 0; --i) {
      if (i == size - 1) {
        result += std::format("{:x}", static_cast<uint64_t>(base[i]));
      } else {
        result += std::format("{:016x}", static_cast<uint64_t>(base[i]));
      }
    }
  }
  return result;
}

std::string Dlop::to_verilog() const {
  if (is_known_false()) return "'sb0";

  if (has_unknowns()) {
    auto bin = to_binary();
    return std::format("{}'sb{}", get_bits(), bin);
  }

  if (type == Type::String) {
    return std::format("\"{}\"", to_string());
  }

  int nbits = get_bits();
  if (is_negative()) {
    auto pos = neg_op();
    // Two's complement: (1 << nbits) - abs
    return std::format("{}'sh{}", nbits, pos->to_pyrope().substr(2));  // skip "0x"
  }

  if (is_i()) {
    return std::format("{}'sh{:x}", nbits, static_cast<uint64_t>(base[0]));
  }

  std::string hex;
  for (int i = size - 1; i >= 0; --i) {
    if (i == size - 1) {
      hex += std::format("{:x}", static_cast<uint64_t>(base[i]));
    } else {
      hex += std::format("{:016x}", static_cast<uint64_t>(base[i]));
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
    std::print("_{:016x}", (uint64_t)base[i]);
  }
  std::print("\n extra:0x");
  for (int i = size - 1; i >= 0; --i) {
    std::print("_{:016x}", (uint64_t)extra[i]);
  }
  std::print("\n");
}
