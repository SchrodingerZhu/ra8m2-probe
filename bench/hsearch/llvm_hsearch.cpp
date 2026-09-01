//===-- Standalone build of llvm-libc's hsearch hash table ----------------===//
// Compiles llvm-libc's __support/HashTable/table.h (from $LIBC_SRC, tip of
// tree) outside the libc build, exposing a small C API for benchmarking.
// Variants (set by -D at compile time; symbol prefix via VARIANT):
//   HS_USE_MVE_GROUP : replace bitmask.h with the MVE 16-byte Group
//   HS_USE_GRP8      : replace bitmask.h with a portable 8-byte SWAR Group
// The hash (64-bit aHash) is IDENTICAL in all variants: only the group /
// probing structure differs. The default (no flags) is exactly what llvm-libc
// builds on 32-bit Arm today: 4-byte SWAR groups.
//===----------------------------------------------------------------------===//

#ifdef HS_USE_MVE_GROUP
#define LLVM_LIBC_SRC___SUPPORT_HASHTABLE_BITMASK_H // bypass bitmask.h
#include "mve_group.h"
#endif

#ifdef HS_USE_CHEAP_HASH
#define LLVM_LIBC_SRC___SUPPORT_HASH_H // bypass hash.h: instrumentation hash
#include "cheaphash.h"
#endif

#ifdef HS_USE_GRP8
#define LLVM_LIBC_SRC___SUPPORT_HASHTABLE_BITMASK_H // bypass bitmask.h
#include "grp8.h"
#endif

#include "src/__support/HashTable/table.h"

#define CONCAT2(a, b) a##b
#define CONCAT(a, b) CONCAT2(a, b)
#define API(name) CONCAT(VARIANT, CONCAT(_, name))

using LIBC_NAMESPACE::internal::HashTable;

extern "C" {

void *API(create)(size_t cap, uint64_t seed) {
  return HashTable::allocate(cap, seed);
}

ENTRY *API(insert)(void **table, ENTRY item) {
  HashTable *t = static_cast<HashTable *>(*table);
  ENTRY *res = HashTable::insert(t, item);
  *table = t;
  return res;
}

ENTRY *API(find)(void *table, const char *key) {
  return static_cast<HashTable *>(table)->find(key);
}

void API(destroy)(void *table) {
  HashTable::deallocate(static_cast<HashTable *>(table));
}

// hash microbenchmark hook: one-shot hash exactly as the table does it
// (copy an existing state, update with the key, finish) — the state is
// constructed once, like HashTable::allocate does.
uint64_t API(hash)(uint64_t seed, const char *key) {
  static LIBC_NAMESPACE::internal::HashState base{
      0x243f6a8885a308d3, 0x13198a2e03707344, 0xa4093822299f31d0,
      0x082efa98ec4e6c89};
  (void)seed;
  LIBC_NAMESPACE::internal::HashState state = base;   // copy, like oneshot_hash
  state.update(key, LIBC_NAMESPACE::internal::string_length(key));
  return state.finish();
}

} // extern "C"
