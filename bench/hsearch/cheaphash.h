//===-- Minimal-cost HashState used to EXPOSE probing costs ---------------===//
// Instrumentation, not a proposal: a 32-bit FNV-1a with the same interface as
// __support/hash.h HashState. Used identically by every group variant so the
// probe/compare path dominates the measurement instead of the hash.
//===----------------------------------------------------------------------===//
#ifndef BENCH_HSEARCH_CHEAPHASH_H
#define BENCH_HSEARCH_CHEAPHASH_H

#include <stddef.h>
#include <stdint.h>
#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

class HashState {
  uint32_t h;
public:
  LIBC_INLINE constexpr HashState(uint64_t a, uint64_t, uint64_t, uint64_t)
      : h(0x811C9DC5u ^ static_cast<uint32_t>(a)) {}
  LIBC_INLINE HashState(uint64_t seed) : HashState(seed, 0, 0, 0) {}
  LIBC_INLINE void update(const void *ptr, size_t size) {
    const uint8_t *b = static_cast<const uint8_t *>(ptr);
    uint32_t x = h;
    for (size_t i = 0; i < size; i++) { x ^= b[i]; x *= 0x01000193u; }
    h = x;
  }
  LIBC_INLINE uint64_t finish() {
    uint32_t x = h;
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15;   // final avalanche
    return (static_cast<uint64_t>(x) << 32) | x;
  }
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
