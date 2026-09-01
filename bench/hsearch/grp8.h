//===-- HashTable BitMasks: 8-byte SWAR group for 32-bit cores ------------===//
// Same algorithm as llvm-libc's generic/bitmask_impl.inc, but with the group
// width fixed to 64 bits instead of uintptr_t (4 bytes on 32-bit Arm). One
// LDRD per probe covers 8 control bytes; the SWAR arithmetic runs on register
// pairs. Portable C++ — no vector unit needed.
// Bypasses __support/HashTable/bitmask.h via its include guard.
//===----------------------------------------------------------------------===//
#ifndef BENCH_HSEARCH_GRP8_H
#define BENCH_HSEARCH_GRP8_H

#include <stddef.h>
#include <stdint.h>

#include "src/__support/CPP/bit.h"
#include "src/__support/endian_internal.h"
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

using bitmask64_t = uint64_t;

LIBC_INLINE constexpr bitmask64_t repeat_byte64(bitmask64_t byte) {
  size_t shift_amount = 8;
  while (shift_amount < sizeof(bitmask64_t) * 8) {
    byte |= byte << shift_amount;
    shift_amount <<= 1;
  }
  return byte;
}

using BitMask = BitMaskAdaptor<bitmask64_t, 0x8ul>;
using IteratableBitMask = IteratableBitMaskAdaptor<BitMask>;

struct Group {
  LIBC_INLINE_VAR static constexpr bitmask64_t MASK = repeat_byte64(0x80ull);
  bitmask64_t data;

  LIBC_INLINE static Group load(const void *addr) {
    bitmask64_t v;
    __builtin_memcpy(&v, addr, sizeof(v));
    return Group{v};
  }

  LIBC_INLINE static Group load_aligned(const void *addr) {
    return *static_cast<const Group *>(addr);
  }

  LIBC_INLINE IteratableBitMask match_byte(uint8_t byte) const {
    static constexpr bitmask64_t ONES = repeat_byte64(0x01ull);
    auto cmp = data ^ repeat_byte64(static_cast<bitmask64_t>(byte));
    auto result =
        LIBC_NAMESPACE::Endian::to_little_endian((cmp - ONES) & ~cmp & MASK);
    return {BitMask{result}};
  }

  LIBC_INLINE BitMask mask_available() const {
    bitmask64_t le_data = LIBC_NAMESPACE::Endian::to_little_endian(data);
    return {le_data & MASK};
  }

  LIBC_INLINE IteratableBitMask occupied() const {
    bitmask64_t available = mask_available().word;
    return {BitMask{available ^ MASK}};
  }
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // BENCH_HSEARCH_GRP8_H
