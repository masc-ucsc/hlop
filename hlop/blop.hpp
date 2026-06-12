// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// blop: Base Logic Operation
//
// Provides low-level arithmetic and bitwise primitives for both:
//   - Dlop (dynamic, runtime-sized): uses pointer+size variants
//   - Slop (static, compile-time-sized): uses std::array<int64_t,N> variants
//
// All operations are signed with unlimited-precision semantics.
// The 64-bit scalar path is the fast common case (most values fit in 63 signed bits).

#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <type_traits>

class Blop {
public:
  // =========================================================================
  // EXTEND (sign-extend a scalar into an array)
  // =========================================================================
  static void extend(int64_t* dest, size_t dest_sz, const int64_t src) {
    dest[0]   = src;
    int64_t v = src < 0 ? -1 : 0;
    for (size_t i = 1; i < dest_sz; ++i) {
      dest[i] = v;
    }
  }

  template <size_t N>
  static void extend(std::array<int64_t, N>& dest, const int64_t src) {
    dest[0]   = src;
    int64_t v = src < 0 ? -1 : 0;
    for (size_t i = 1; i < N; ++i) {
      dest[i] = v;
    }
  }

  // =========================================================================
  // ADD
  // =========================================================================
  static void add64(int64_t& dest, const int64_t src1, const int64_t src2) { dest = src1 + src2; }

  static void addn(int64_t* dest, size_t dest_sz, const int64_t* src1, const int64_t* src2) {
    assert(dest_sz >= 1);
    uint64_t carry = __builtin_uaddll_overflow(src1[0], src2[0], reinterpret_cast<unsigned long long*>(dest));
    for (size_t i = 1; i < dest_sz - 1; ++i) {
      unsigned long long tmp;
      carry  = __builtin_uaddll_overflow(src1[i], carry, &tmp);
      carry |= __builtin_uaddll_overflow(tmp, src2[i], reinterpret_cast<unsigned long long*>(dest + i));
    }
    if (dest_sz > 1) {
      dest[dest_sz - 1] = src1[dest_sz - 1] + src2[dest_sz - 1] + carry;
    }
  }

  template <size_t N>
  static constexpr void add(std::array<int64_t, N>& dest, const std::array<int64_t, N>& src1, const std::array<int64_t, N>& src2) {
    if constexpr (N == 1) {
      dest[0] = src1[0] + src2[0];
    } else {
      uint64_t carry = __builtin_uaddll_overflow(src1[0], src2[0], reinterpret_cast<unsigned long long*>(&dest[0]));
      for (size_t i = 1; i < N - 1; ++i) {
        unsigned long long tmp;
        carry  = __builtin_uaddll_overflow(src1[i], carry, &tmp);
        carry |= __builtin_uaddll_overflow(tmp, src2[i], reinterpret_cast<unsigned long long*>(&dest[i]));
      }
      dest[N - 1] = src1[N - 1] + src2[N - 1] + carry;
    }
  }

  // =========================================================================
  // SUB
  // =========================================================================
  static void sub64(int64_t& dest, const int64_t src1, const int64_t src2) { dest = src1 - src2; }

  static void subn(int64_t* dest, size_t dest_sz, const int64_t* src1, const int64_t* src2) {
    assert(dest_sz >= 1);
    uint64_t carry = __builtin_usubll_overflow(src1[0], src2[0], reinterpret_cast<unsigned long long*>(dest));
    for (size_t i = 1; i < dest_sz - 1; ++i) {
      unsigned long long tmp;
      carry  = __builtin_usubll_overflow(src1[i], carry, &tmp);
      carry |= __builtin_usubll_overflow(tmp, src2[i], reinterpret_cast<unsigned long long*>(dest + i));
    }
    if (dest_sz > 1) {
      dest[dest_sz - 1] = src1[dest_sz - 1] - src2[dest_sz - 1] - carry;
    }
  }

  template <size_t N>
  static void sub(std::array<int64_t, N>& dest, const std::array<int64_t, N>& src1, const std::array<int64_t, N>& src2) {
    if constexpr (N == 1) {
      dest[0] = src1[0] - src2[0];
    } else {
      uint64_t carry = __builtin_usubll_overflow(src1[0], src2[0], reinterpret_cast<unsigned long long*>(&dest[0]));
      for (size_t i = 1; i < N - 1; ++i) {
        unsigned long long tmp;
        carry  = __builtin_usubll_overflow(src1[i], carry, &tmp);
        carry |= __builtin_usubll_overflow(tmp, src2[i], reinterpret_cast<unsigned long long*>(&dest[i]));
      }
      dest[N - 1] = src1[N - 1] - src2[N - 1] - carry;
    }
  }

  // =========================================================================
  // NEG (two's complement negate: -x == ~x + 1)
  // =========================================================================
  static void neg64(int64_t& dest, const int64_t src) { dest = -src; }

  static void negn(int64_t* dest, size_t dest_sz, const int64_t* src) {
    assert(dest_sz >= 1);
    // ~src + 1
    uint64_t carry = 1;
    for (size_t i = 0; i < dest_sz; ++i) {
      uint64_t flipped = ~static_cast<uint64_t>(src[i]);
      uint64_t sum     = flipped + carry;
      carry            = (sum < flipped) ? 1 : 0;
      dest[i]          = static_cast<int64_t>(sum);
    }
  }

  template <size_t N>
  static constexpr void neg(std::array<int64_t, N>& dest, const std::array<int64_t, N>& src) {
    if constexpr (N == 1) {
      dest[0] = -src[0];
    } else {
      uint64_t carry = 1;
      for (size_t i = 0; i < N; ++i) {
        uint64_t flipped = ~static_cast<uint64_t>(src[i]);
        uint64_t sum     = flipped + carry;
        carry            = (sum < flipped) ? 1 : 0;
        dest[i]          = static_cast<int64_t>(sum);
      }
    }
  }

  // =========================================================================
  // SHL (shift left)
  // =========================================================================
  static void shl64(int64_t& dest, const int64_t src1, const int64_t src2) {
    assert(src2 >= 0 && src2 < 64);
    dest = src1 << src2;
  }

  static void shln(int64_t* dest, size_t dest_sz, const int64_t* src1, const int64_t src2) {
    assert(dest_sz >= 1);
    assert(src2 >= 0);

    uint64_t word_up = src2 / 64;
    assert(word_up < dest_sz);
    uint64_t bits_up = src2 & 63;

    if (bits_up == 0) {
      for (int i = dest_sz - word_up - 1; i >= 0; --i) {
        dest[i + word_up] = src1[i];
      }
    } else {
      for (int i = dest_sz - word_up - 1; i > 0; --i) {
        auto tmp           = src1[i];
        dest[i + word_up]  = static_cast<uint64_t>(src1[i - 1]) >> (64 - bits_up);
        dest[i + word_up] |= tmp << bits_up;
      }
      dest[word_up] = src1[0] << bits_up;
    }
    for (size_t i = 0; i < word_up; ++i) {
      dest[i] = 0;
    }
  }

  template <size_t N>
  static constexpr void shl(std::array<int64_t, N>& dest, const std::array<int64_t, N>& src1, int64_t amount) {
    if constexpr (N == 1) {
      assert(amount >= 0 && amount < 64);
      dest[0] = src1[0] << amount;
    } else {
      assert(amount >= 0);
      size_t word_up = amount / 64;
      assert(word_up < N);
      size_t bits_up = amount & 63;

      if (bits_up == 0) {
        for (int i = N - word_up - 1; i >= 0; --i) {
          dest[i + word_up] = src1[i];
        }
      } else {
        for (int i = N - word_up - 1; i > 0; --i) {
          auto tmp           = src1[i];
          dest[i + word_up]  = static_cast<uint64_t>(src1[i - 1]) >> (64 - bits_up);
          dest[i + word_up] |= tmp << bits_up;
        }
        dest[word_up] = src1[0] << bits_up;
      }
      for (size_t i = 0; i < word_up; ++i) {
        dest[i] = 0;
      }
    }
  }

  // =========================================================================
  // SHR (arithmetic shift right — sign-extending)
  // =========================================================================
  static void shr64(int64_t& dest, const int64_t src1, const int64_t src2) {
    assert(src2 >= 0 && src2 < 64);
    dest = src1 >> src2;
  }

  static void shrn(int64_t* dest, size_t dest_sz, const int64_t* src1, const int64_t src2) {
    assert(dest_sz >= 1);
    assert(src2 >= 0);

    size_t word_down = src2 / 64;
    size_t bits_down = src2 & 63;

    int64_t sign_fill = src1[dest_sz - 1] < 0 ? -1 : 0;

    if (bits_down == 0) {
      for (size_t i = word_down; i < dest_sz; i++) {
        dest[i - word_down] = src1[i];
      }
    } else {
      for (size_t i = word_down; i < dest_sz - 1; i++) {
        auto tmp             = static_cast<uint64_t>(src1[i]) >> bits_down;
        tmp                 |= static_cast<uint64_t>(src1[i + 1]) << (64 - bits_down);
        dest[i - word_down]  = tmp;
      }
      dest[dest_sz - 1 - word_down] = src1[dest_sz - 1] >> bits_down;  // arithmetic shift on top word
    }
    // Fill upper words with sign
    for (size_t i = dest_sz - word_down; i < dest_sz; ++i) {
      dest[i] = sign_fill;
    }
  }

  template <size_t N>
  static void shr(std::array<int64_t, N>& dest, const std::array<int64_t, N>& src1, int64_t amount) {
    if constexpr (N == 1) {
      assert(amount >= 0 && amount < 64);
      dest[0] = src1[0] >> amount;
    } else {
      assert(amount >= 0);
      size_t word_down = amount / 64;
      size_t bits_down = amount & 63;

      int64_t sign_fill = src1[N - 1] < 0 ? -1 : 0;

      if (bits_down == 0) {
        for (size_t i = word_down; i < N; i++) {
          dest[i - word_down] = src1[i];
        }
      } else {
        for (size_t i = word_down; i < N - 1; i++) {
          auto tmp             = static_cast<uint64_t>(src1[i]) >> bits_down;
          tmp                 |= static_cast<uint64_t>(src1[i + 1]) << (64 - bits_down);
          dest[i - word_down]  = tmp;
        }
        dest[N - 1 - word_down] = src1[N - 1] >> bits_down;
      }
      for (size_t i = N - word_down; i < N; ++i) {
        dest[i] = sign_fill;
      }
    }
  }

  // =========================================================================
  // OR
  // =========================================================================
  static void or64(int64_t& dest, const int64_t src1, const int64_t src2) { dest = src1 | src2; }

  static void orn(int64_t* dest, size_t dest_sz, const int64_t* src1, const int64_t* src2) {
    for (size_t i = 0; i < dest_sz; i++) {
      dest[i] = src1[i] | src2[i];
    }
  }

  static void orn(int64_t* dest, size_t dest_sz, const int64_t* src1, const int64_t src2) {
    dest[0]   = src1[0] | src2;
    int64_t v = src2 < 0 ? -1 : 0;
    for (size_t i = 1; i < dest_sz; i++) {
      dest[i] = src1[i] | v;
    }
  }

  template <size_t N>
  static void bor(std::array<int64_t, N>& dest, const std::array<int64_t, N>& src1, const std::array<int64_t, N>& src2) {
    for (size_t i = 0; i < N; i++) {
      dest[i] = src1[i] | src2[i];
    }
  }

  // =========================================================================
  // AND
  // =========================================================================
  static void and64(int64_t& dest, const int64_t src1, const int64_t src2) { dest = src1 & src2; }

  static void andn(int64_t* dest, size_t dest_sz, const int64_t* src1, const int64_t* src2) {
    for (size_t i = 0; i < dest_sz; i++) {
      dest[i] = src1[i] & src2[i];
    }
  }

  template <size_t N>
  static void band(std::array<int64_t, N>& dest, const std::array<int64_t, N>& src1, const std::array<int64_t, N>& src2) {
    for (size_t i = 0; i < N; i++) {
      dest[i] = src1[i] & src2[i];
    }
  }

  // =========================================================================
  // XOR
  // =========================================================================
  static void xor64(int64_t& dest, const int64_t src1, const int64_t src2) { dest = src1 ^ src2; }

  static void xorn(int64_t* dest, size_t dest_sz, const int64_t* src1, const int64_t* src2) {
    for (size_t i = 0; i < dest_sz; i++) {
      dest[i] = src1[i] ^ src2[i];
    }
  }

  template <size_t N>
  static void bxor(std::array<int64_t, N>& dest, const std::array<int64_t, N>& src1, const std::array<int64_t, N>& src2) {
    for (size_t i = 0; i < N; i++) {
      dest[i] = src1[i] ^ src2[i];
    }
  }

  // =========================================================================
  // NOT (bitwise complement: ~x == -x - 1)
  // =========================================================================
  static void not64(int64_t& dest, const int64_t src) { dest = ~src; }

  static void notn(int64_t* dest, size_t dest_sz, const int64_t* src) {
    for (size_t i = 0; i < dest_sz; i++) {
      dest[i] = ~src[i];
    }
  }

  template <size_t N>
  static void bnot(std::array<int64_t, N>& dest, const std::array<int64_t, N>& src) {
    for (size_t i = 0; i < N; i++) {
      dest[i] = ~src[i];
    }
  }

  // =========================================================================
  // MULT (schoolbook multiplication using __uint128_t)
  // =========================================================================
  static void mult64(int64_t& dest, const int64_t src1, const int64_t src2) { dest = src1 * src2; }

  // Multi-word signed multiply: dest[0..dest_sz-1] = src1[0..src1_sz-1] * src2[0..src2_sz-1]
  // dest_sz must be >= src1_sz + src2_sz to avoid overflow.
  // All arrays are in signed two's complement.
  static void multn(int64_t* dest, size_t dest_sz, const int64_t* src1, size_t src1_sz, const int64_t* src2, size_t src2_sz) {
    using u128 = unsigned __int128;

    // Clear dest
    for (size_t i = 0; i < dest_sz; i++) {
      dest[i] = 0;
    }

    // Unsigned schoolbook multiply of magnitudes, then fix sign
    // But for two's complement, we can do unsigned multiply directly
    // if we sign-extend conceptually. The schoolbook works on unsigned
    // words and the two's complement representation handles sign naturally
    // as long as dest is large enough.

    for (size_t j = 0; j < src2_sz; ++j) {
      uint64_t carry = 0;
      for (size_t i = 0; i < src1_sz; ++i) {
        if (i + j >= dest_sz) {
          break;
        }
        u128 prod   = static_cast<u128>(static_cast<uint64_t>(src1[i])) * static_cast<uint64_t>(src2[j])
                      + static_cast<uint64_t>(dest[i + j]) + carry;
        dest[i + j] = static_cast<int64_t>(static_cast<uint64_t>(prod));
        carry       = static_cast<uint64_t>(prod >> 64);
      }
      if (j + src1_sz < dest_sz) {
        dest[j + src1_sz] += carry;
      }
    }

    // Fix for signed: if src2 is negative, subtract src1 shifted left by src2_sz words
    // (This accounts for the two's complement of src2's top word)
    if (src2[src2_sz - 1] < 0) {
      for (size_t i = 0; i < src1_sz && (i + src2_sz) < dest_sz; ++i) {
        dest[i + src2_sz] -= src1[i];
      }
    }
    if (src1[src1_sz - 1] < 0) {
      for (size_t j = 0; j < src2_sz && (j + src1_sz) < dest_sz; ++j) {
        dest[j + src1_sz] -= src2[j];
      }
    }
  }

  // Multiply multi-word by scalar (4-arg compat: dest_sz == src1_sz)
  static void multn(int64_t* dest, size_t dest_sz, const int64_t* src1, const int64_t src2) {
    multn(dest, dest_sz, src1, dest_sz, src2);
  }

  // Multiply multi-word by scalar
  static void multn(int64_t* dest, size_t dest_sz, const int64_t* src1, size_t src1_sz, const int64_t src2) {
    using u128 = unsigned __int128;

    for (size_t i = 0; i < dest_sz; i++) {
      dest[i] = 0;
    }

    uint64_t carry = 0;
    for (size_t i = 0; i < src1_sz && i < dest_sz; ++i) {
      u128 prod = static_cast<u128>(static_cast<uint64_t>(src1[i])) * static_cast<uint64_t>(src2) + carry;
      dest[i]   = static_cast<int64_t>(static_cast<uint64_t>(prod));
      carry     = static_cast<uint64_t>(prod >> 64);
    }
    if (src1_sz < dest_sz) {
      dest[src1_sz] += carry;
    }

    // Sign correction
    if (src2 < 0) {
      for (size_t i = 0; i < src1_sz && (i + 1) < dest_sz; ++i) {
        dest[i + 1] -= src1[i];
      }
    }
    if (src1[src1_sz - 1] < 0) {
      dest[src1_sz] -= src2;  // only 1 word for src2
    }
  }

  template <size_t N>
  static constexpr void mult(std::array<int64_t, N>& dest, const std::array<int64_t, N>& src1, const std::array<int64_t, N>& src2) {
    if constexpr (N == 1) {
      dest[0] = src1[0] * src2[0];
    } else {
      // Multi-word case is not constexpr-evaluable (uses pointer arithmetic
      // through `multn`). For consteval contexts, callers are expected to
      // stay within the N==1 fast path.
      if (std::is_constant_evaluated()) {
        // Schoolbook multiplication on the constant-evaluation path.
        std::array<int64_t, N> tmp{};
        for (size_t i = 0; i < N; ++i) {
          uint64_t carry = 0;
          for (size_t j = 0; j + i < N; ++j) {
            __uint128_t prod = static_cast<__uint128_t>(static_cast<uint64_t>(src1[i]))
                                   * static_cast<__uint128_t>(static_cast<uint64_t>(src2[j]))
                               + static_cast<__uint128_t>(static_cast<uint64_t>(tmp[i + j])) + carry;
            tmp[i + j]       = static_cast<int64_t>(static_cast<uint64_t>(prod));
            carry            = static_cast<uint64_t>(prod >> 64);
          }
        }
        dest = tmp;
      } else {
        multn(dest.data(), N, src1.data(), N, src2.data(), N);
      }
    }
  }

  // =========================================================================
  // DIV (signed division, truncating toward zero)
  // =========================================================================
  static void div64(int64_t& dest, const int64_t src1, const int64_t src2) {
    assert(src2 != 0);
    dest = src1 / src2;
  }

  // Multi-word division is complex. For now, provide a 64-bit fast path
  // and a helper that converts to/from single words when possible.
  // Full Knuth Algorithm D can be added later if needed.
  static void divn(int64_t* dest, size_t dest_sz, const int64_t* src1, size_t src1_sz, const int64_t* src2, size_t src2_sz) {
    // Check if both fit in 64 bits
    bool    s1_fits = true;
    int64_t s1_sign = src1[src1_sz - 1] < 0 ? -1 : 0;
    for (size_t i = 1; i < src1_sz; ++i) {
      if (src1[i] != s1_sign) {
        s1_fits = false;
        break;
      }
    }

    bool    s2_fits = true;
    int64_t s2_sign = src2[src2_sz - 1] < 0 ? -1 : 0;
    for (size_t i = 1; i < src2_sz; ++i) {
      if (src2[i] != s2_sign) {
        s2_fits = false;
        break;
      }
    }

    if (s1_fits && s2_fits) {
      assert(src2[0] != 0);
      int64_t result = src1[0] / src2[0];
      dest[0]        = result;
      int64_t fill   = result < 0 ? -1 : 0;
      for (size_t i = 1; i < dest_sz; ++i) {
        dest[i] = fill;
      }
      return;
    }

    // TODO: Knuth Algorithm D for full multi-word division
    assert(false && "Multi-word division beyond 64-bit not yet implemented");
  }

  template <size_t N>
  static void div(std::array<int64_t, N>& dest, const std::array<int64_t, N>& src1, const std::array<int64_t, N>& src2) {
    if constexpr (N == 1) {
      assert(src2[0] != 0);
      dest[0] = src1[0] / src2[0];
    } else {
      divn(dest.data(), N, src1.data(), N, src2.data(), N);
    }
  }

  // =========================================================================
  // EQ (equality comparison, returns bool)
  // =========================================================================
  static bool eq64(const int64_t src1, const int64_t src2) { return src1 == src2; }

  static bool eqn(const int64_t* src1, const int64_t* src2, size_t sz) {
    for (size_t i = 0; i < sz; ++i) {
      if (src1[i] != src2[i]) {
        return false;
      }
    }
    return true;
  }

  template <size_t N>
  static bool eq(const std::array<int64_t, N>& src1, const std::array<int64_t, N>& src2) {
    for (size_t i = 0; i < N; ++i) {
      if (src1[i] != src2[i]) {
        return false;
      }
    }
    return true;
  }

  // =========================================================================
  // LT (signed less-than comparison)
  // =========================================================================
  static bool lt64(const int64_t src1, const int64_t src2) { return src1 < src2; }

  static bool ltn(const int64_t* src1, const int64_t* src2, size_t sz) {
    // Compare from MSW down; top word is signed, rest are unsigned magnitude
    if (src1[sz - 1] != src2[sz - 1]) {
      return src1[sz - 1] < src2[sz - 1];  // signed compare on top word
    }
    for (int i = static_cast<int>(sz) - 2; i >= 0; --i) {
      if (static_cast<uint64_t>(src1[i]) != static_cast<uint64_t>(src2[i])) {
        return static_cast<uint64_t>(src1[i]) < static_cast<uint64_t>(src2[i]);
      }
    }
    return false;  // equal
  }

  template <size_t N>
  static bool lt(const std::array<int64_t, N>& src1, const std::array<int64_t, N>& src2) {
    if constexpr (N == 1) {
      return src1[0] < src2[0];
    } else {
      return ltn(src1.data(), src2.data(), N);
    }
  }

  // =========================================================================
  // IS_NEGATIVE / IS_ZERO
  // =========================================================================
  static bool is_negative64(const int64_t src) { return src < 0; }

  static bool is_negativen(const int64_t* src, size_t sz) { return src[sz - 1] < 0; }

  template <size_t N>
  static bool is_negative(const std::array<int64_t, N>& src) {
    return src[N - 1] < 0;
  }

  static bool is_zero64(const int64_t src) { return src == 0; }

  static bool is_zeron(const int64_t* src, size_t sz) {
    for (size_t i = 0; i < sz; ++i) {
      if (src[i] != 0) {
        return false;
      }
    }
    return true;
  }

  template <size_t N>
  static bool is_zero(const std::array<int64_t, N>& src) {
    for (size_t i = 0; i < N; ++i) {
      if (src[i] != 0) {
        return false;
      }
    }
    return true;
  }

  // =========================================================================
  // POPCOUNT
  // =========================================================================
  static int popcount64(const int64_t src) { return __builtin_popcountll(static_cast<uint64_t>(src)); }

  static int popcountn(const int64_t* src, size_t sz) {
    int count = 0;
    for (size_t i = 0; i < sz; ++i) {
      count += __builtin_popcountll(static_cast<uint64_t>(src[i]));
    }
    return count;
  }

  template <size_t N>
  static int popcount(const std::array<int64_t, N>& src) {
    int count = 0;
    for (size_t i = 0; i < N; ++i) {
      count += __builtin_popcountll(static_cast<uint64_t>(src[i]));
    }
    return count;
  }

  // =========================================================================
  // CLZ (count leading zeros from MSB of the top word)
  // Returns the number of leading sign bits - 1 (i.e., how many bits match the sign)
  // =========================================================================
  static int clz64(const uint64_t src) {
    if (src == 0) {
      return 64;
    }
    return __builtin_clzll(src);
  }

  // =========================================================================
  // CTZ (count trailing zeros)
  // =========================================================================
  static int ctz64(const uint64_t src) {
    if (src == 0) {
      return 64;
    }
    return __builtin_ctzll(src);
  }

  static int ctzn(const int64_t* src, size_t sz) {
    for (size_t i = 0; i < sz; ++i) {
      if (src[i] != 0) {
        return i * 64 + __builtin_ctzll(static_cast<uint64_t>(src[i]));
      }
    }
    return sz * 64;
  }

  template <size_t N>
  static int ctz(const std::array<int64_t, N>& src) {
    for (size_t i = 0; i < N; ++i) {
      if (src[i] != 0) {
        return i * 64 + __builtin_ctzll(static_cast<uint64_t>(src[i]));
      }
    }
    return N * 64;
  }

  // =========================================================================
  // BIT_TEST (test a specific bit position)
  // =========================================================================
  static bool bit_test64(const int64_t src, int pos) {
    assert(pos >= 0 && pos < 64);
    return (src >> pos) & 1;
  }

  static bool bit_testn(const int64_t* src, [[maybe_unused]] size_t sz, int pos) {
    int word = pos / 64;
    int bit  = pos % 64;
    assert(word >= 0 && static_cast<size_t>(word) < sz);
    return (src[word] >> bit) & 1;
  }

  template <size_t N>
  static bool bit_test(const std::array<int64_t, N>& src, int pos) {
    int word = pos / 64;
    int bit  = pos % 64;
    assert(word >= 0 && static_cast<size_t>(word) < N);
    return (src[word] >> bit) & 1;
  }

  // =========================================================================
  // SEXT (sign-extend from bit position 'from_bit')
  // Bits above from_bit are set to match the value of bit at from_bit.
  // =========================================================================
  static void sext64(int64_t& dest, const int64_t src, int from_bit) {
    assert(from_bit >= 0 && from_bit < 64);
    if (from_bit == 63) {
      dest = src;
      return;
    }
    int64_t mask = (int64_t(1) << (from_bit + 1)) - 1;
    if ((src >> from_bit) & 1) {
      dest = src | ~mask;  // sign bit is 1, fill upper with 1s
    } else {
      dest = src & mask;  // sign bit is 0, fill upper with 0s
    }
  }

  static void sextn(int64_t* dest, size_t dest_sz, const int64_t* src, int from_bit) {
    assert(from_bit >= 0);
    int word = from_bit / 64;
    int bit  = from_bit % 64;

    // Copy lower words unchanged
    for (int i = 0; i < word && static_cast<size_t>(i) < dest_sz; ++i) {
      dest[i] = src[i];
    }

    if (static_cast<size_t>(word) < dest_sz) {
      // Sign extend within the word containing from_bit
      int64_t mask     = (int64_t(1) << (bit + 1)) - 1;
      bool    sign_bit = (src[word] >> bit) & 1;
      if (sign_bit) {
        dest[word] = src[word] | ~mask;
      } else {
        dest[word] = src[word] & mask;
      }

      // Fill remaining words with sign
      int64_t fill = sign_bit ? -1 : 0;
      for (size_t i = word + 1; i < dest_sz; ++i) {
        dest[i] = fill;
      }
    }
  }

  template <size_t N>
  static void sext(std::array<int64_t, N>& dest, const std::array<int64_t, N>& src, int from_bit) {
    if constexpr (N == 1) {
      sext64(dest[0], src[0], from_bit);
    } else {
      sextn(dest.data(), N, src.data(), from_bit);
    }
  }

  // =========================================================================
  // GET_BITS: number of signed bits needed to represent the value
  // A value of 0 needs 0 bits. A value of -1 needs 1 bit.
  // A positive value v needs floor(log2(v)) + 2 bits (one for sign).
  // A negative value v needs floor(log2(-v-1)) + 2 bits.
  // =========================================================================
  static constexpr int get_bits64(const int64_t src) {
    if (src == 0) {
      return 0;
    }
    if (src == -1) {
      return 1;
    }
    if (src > 0) {
      return 64 - __builtin_clzll(static_cast<uint64_t>(src)) + 1;
    }
    return 64 - __builtin_clzll(static_cast<uint64_t>(-(src + 1))) + 1;
  }

  static constexpr int get_bitsn(const int64_t* src, size_t sz) {
    int64_t sign = src[sz - 1] < 0 ? -1 : 0;

    // Find topmost word that differs from sign extension
    int top = sz - 1;
    while (top > 0 && src[top] == sign) {
      --top;
    }

    if (top == 0) {
      // The remaining word must be read with the ORIGINAL sign: a positive
      // value whose low word has bit 63 set (e.g. {-1,0} == 2^64-1) is not
      // the int64 -1, and a negative one reaching into bit 63 (e.g.
      // {0,-1} == -2^64) is not the int64 0. Both need the full 65 bits.
      if (sign == 0 && src[0] < 0) {
        return 65;
      }
      if (sign == -1 && src[0] >= 0) {
        return 65;
      }
      return get_bits64(src[0]);
    }

    // The value at src[top] is significant
    if (sign == 0) {
      // Positive: count bits in top word + lower words
      return top * 64 + 64 - __builtin_clzll(static_cast<uint64_t>(src[top])) + 1;
    } else {
      // Negative: count bits in ~top word
      uint64_t flipped = ~static_cast<uint64_t>(src[top]);
      if (flipped == 0) {
        // All 1s in this word but differs from sign — check one below
        return top * 64 + 1;
      }
      return top * 64 + 64 - __builtin_clzll(flipped) + 1;
    }
  }

  template <size_t N>
  static constexpr int get_bits(const std::array<int64_t, N>& src) {
    if constexpr (N == 1) {
      return get_bits64(src[0]);
    } else {
      return get_bitsn(src.data(), N);
    }
  }

  // =========================================================================
  // MSB position (index of highest set bit, for positive numbers)
  // =========================================================================
  static int msb64(const uint64_t src) {
    assert(src != 0);
    return 63 - __builtin_clzll(src);
  }

  static int msbn(const int64_t* src, size_t sz) {
    for (int i = sz - 1; i >= 0; --i) {
      if (static_cast<uint64_t>(src[i]) != 0) {
        return i * 64 + 63 - __builtin_clzll(static_cast<uint64_t>(src[i]));
      }
    }
    return -1;  // all zero
  }

  template <size_t N>
  static int msb(const std::array<int64_t, N>& src) {
    if constexpr (N == 1) {
      if (src[0] == 0) {
        return -1;
      }
      return msb64(static_cast<uint64_t>(src[0]));
    } else {
      return msbn(src.data(), N);
    }
  }
};
