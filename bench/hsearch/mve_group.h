//===-- HashTable BitMasks: Armv8.1-M MVE implementation ------------------===//
// Port of llvm-libc's sse2/bitmask_impl.inc to MVE (Helium): a Group is 16
// control bytes; VCMP produces the 16-bit lane mask directly in P0 (the exact
// equivalent of _mm_movemask_epi8). Drop-in for __support/HashTable/bitmask.h:
// define the bitmask.h include guard and include this instead.
// The BitMaskAdaptor/IteratableBitMaskAdaptor templates are copied verbatim
// from bitmask.h (which we bypass entirely).
//===----------------------------------------------------------------------===//
#ifndef BENCH_HSEARCH_MVE_GROUP_H
#define BENCH_HSEARCH_MVE_GROUP_H

#include <arm_mve.h>
#include <stddef.h>
#include <stdint.h>

#include "src/__support/CPP/bit.h"
#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

template <typename T, size_t WORD_STRIDE> struct BitMaskAdaptor {
  LIBC_INLINE_VAR constexpr static size_t STRIDE = WORD_STRIDE;
  T word;
  LIBC_INLINE constexpr bool any_bit_set() const { return word != 0; }
  LIBC_INLINE constexpr size_t lowest_set_bit_nonzero() const {
    return cpp::countr_zero<T>(word) / WORD_STRIDE;
  }
};

template <class BitMask> struct IteratableBitMaskAdaptor : public BitMask {
  LIBC_INLINE void remove_lowest_bit() {
    this->word = this->word & (this->word - 1);
  }
  using value_type = size_t;
  using iterator = BitMask;
  using const_iterator = BitMask;
  LIBC_INLINE size_t operator*() const {
    return this->lowest_set_bit_nonzero();
  }
  LIBC_INLINE IteratableBitMaskAdaptor &operator++() {
    this->remove_lowest_bit();
    return *this;
  }
  LIBC_INLINE IteratableBitMaskAdaptor begin() { return *this; }
  LIBC_INLINE IteratableBitMaskAdaptor end() { return {BitMask{0}}; }
  LIBC_INLINE bool operator==(const IteratableBitMaskAdaptor &other) {
    return this->word == other.word;
  }
  LIBC_INLINE bool operator!=(const IteratableBitMaskAdaptor &other) {
    return this->word != other.word;
  }
};

// With MVE, every bitmask is iterable: one bit per control byte.
using BitMask = BitMaskAdaptor<uint16_t, 0x1u>;
using IteratableBitMask = IteratableBitMaskAdaptor<BitMask>;

struct Group {
  uint8x16_t data;

  LIBC_INLINE static Group load(const void *addr) {
    return {vld1q_u8(static_cast<const uint8_t *>(addr))};
  }

  LIBC_INLINE static Group load_aligned(const void *addr) {
    return {vld1q_u8(static_cast<const uint8_t *>(addr))};
  }

  // Lanes equal to `byte` -> one bit per lane (VCMP.I8 EQ; P0 mask).
  LIBC_INLINE IteratableBitMask match_byte(uint8_t byte) const {
    uint16_t m = vcmpeqq_n_u8(data, byte);
    return {{m}};
  }

  // Available slots have the MSB set (0b1xxx'xxxx): signed compare < 0.
  LIBC_INLINE BitMask mask_available() const {
    uint16_t m = vcmpltq_n_s8(vreinterpretq_s8_u8(data), 0);
    return {m};
  }

  LIBC_INLINE IteratableBitMask occupied() const {
    return {{static_cast<uint16_t>(~mask_available().word)}};
  }
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // BENCH_HSEARCH_MVE_GROUP_H
