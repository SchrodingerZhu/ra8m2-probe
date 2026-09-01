# llvm-libc `hsearch` on Cortex-M85: probing & table structure

Standalone build of llvm-libc's hash table (`__support/HashTable/table.h`, tip of tree
`593d6710`) benchmarked on the RA8M2. The table is a simplified SwissTable: 7-bit secondary
hash in control bytes, group-wise probing over triangular numbers, 7/8 max load, no
deletion. On 32-bit Arm the group is **4 control bytes** (SWAR on `uintptr_t`).

Goal: evaluate the **probing / table structure** with the hash held constant.
Full tables: [RESULTS.md](RESULTS.md), raw: [results.csv](results.csv).

## Standalone port

`llvm_hsearch.cpp` compiles the real `table.h` from the llvm-project checkout with a small
C API (`create/insert/find/destroy/hash`), a bump-`sbrk` + `aligned_alloc` shim
([shim.c](shim.c)), and one compile-time-selected replacement per experiment:

| variant | group | change |
|---|---|---|
| `llvm` | 4 B SWAR | tip of tree as built for 32-bit Arm today |
| `llvm+grp8` | 8 B SWAR | [grp8.h](grp8.h): `uint64_t` SWAR group (one LDRD per probe) |
| `llvm+mveGroup` | 16 B MVE | [mve_group.h](mve_group.h): SSE2-style group via `vld1q`/`vcmpeqq` — `VCMP` yields the 16-lane bitmask directly (the `_mm_movemask_epi8` equivalent) |
| `fnv1a/*` | same three | same groups with a minimal FNV-1a hash ([cheaphash.h](cheaphash.h)) so the probe/compare path dominates the measurement |

Same 64-bit aHash in the first three; same FNV-1a in the last three — only the group
structure varies within each family.

**Benchmarking pitfall worth knowing:** everything in `table.h` is `LIBC_INLINE` (weak,
identical mangled names). Linking several variants with a shared `LIBC_NAMESPACE`
deduplicates `HashTable::find`/`insert` across TUs, so all "variants" silently run the same
code — and crash once layouts diverge. Each variant needs its own `LIBC_NAMESPACE`.

## Method

EK-RA8M2 (Cortex-M85 r1p1, I+D cache on, ≈8 MHz reset clock), DWT_CYCCNT, keys are random
identifiers of 4–19 chars, lookups in shuffled order, misses use never-inserted keys,
finds are best-of-3 passes, all results verified (0 failures). Two load points: the default
sizing (≈50 % load after `bit_ceil(n·8/7)`) and **exact 7/8 fill** (the state just before
resize, longest probe runs).

## Results (cycles per operation)

### Lookup, table at 7/8 load, n = 3584

| | find_hit | find_miss | insert |
|---|---|---|---|
| llvm (aHash64, grp4) | 849 | 811 | 816 |
| llvm+grp8 | 851 | 821 | 828 |
| llvm+mveGroup | 842 | 796 | 823 |
| fnv1a/grp4 | 288 | 246 | 256 |
| fnv1a/grp8 | 291 | 255 | 270 |
| fnv1a/grp16mve | **274** | **235** | 263 |

### Attribution (n=1024, hit ≈ hash + strlen + probe + strcmp)

| | cycles/key |
|---|---|
| aHash64 one-shot hash (as `oneshot_hash` does it) | **698** |
| FNV-1a one-shot hash | 133 |
| whole find_hit with aHash64 | 838 |
| whole find_hit with FNV-1a | 275 |

## Findings

1. **Probing is not the bottleneck of this table.** With the stock hash, group width
   (4 B / 8 B / 16 B-MVE) changes lookups by <1 %: the 64-bit aHash is ~83 % of every
   operation on this 32-bit core (`folded_multiply` = 64×64→128 through llvm-libc's
   software `UInt128`). Any probing optimisation is Amdahl-capped at ~17 %.
2. With the hash controlled to be cheap, the structure signal appears:
   * **MVE 16-byte group: ~4–5 % faster lookups** than the current 4-byte group
     (274 vs 288 hit, 235 vs 246 miss at 7/8 load), slightly slower single-slot inserts
     (`set_ctrl`'s second mirror write and `vmrs` mask extraction offset part of the gain).
   * **8-byte SWAR group is a consistent ~3–4 % regression** on a 32-bit core: the SWAR
     arithmetic runs on register pairs and probes almost never need the extra width.
   * Even at 7/8 load the triangular probe + 7-bit secondary filter keep candidate chains
     short — high load costs only ~25–45 cycles over 50 % load, in every variant.
3. The structure itself is already lean: peak heap is identical across groups
   (18 B/slot at the default ≈50 % post-sizing load; 4096 entries → 72 KiB), growth is
   ~2.7× the cost of a presized insert per key (fnv1a: 634 vs 254), dominated by rehashing
   every key during `grow()`.
4. Two-pass key read: `find()` runs `string_length(key)` and then the hash re-reads the
   key. For short keys that's ~30 cycles of the ~140-cycle non-hash remainder. A fused
   length+hash loop (or `strlen`-free incremental hashing) is the largest *structural*
   saving left after the group width.

## Suggested changes to llvm-libc, in order of value/risk

1. *(structure)* Optional **MVE `bitmask_impl.inc`** (`mve_group.h` here is the prototype,
   ~40 lines): guarded by `__ARM_FEATURE_MVE & 1`, mirrors the SSE2 backend. Worth ~5 % on
   lookups today, and the margin grows if the hash gets cheaper.
2. *(structure)* Do **not** widen the generic SWAR group beyond the native word: measured
   regression on 32-bit.
3. *(structure)* Fuse `string_length` into the one-shot hash (single pass over the key).
4. *(out of scope here, but the measured elephant)* On 32-bit targets the hash itself is
   83 % of every operation; `folded_multiply`'s software UInt128 is the reason. Any
   probing/structure work should be sequenced after (or together with) a 32-bit-friendly
   hash path.

## Reproduce

```sh
nix develop ../.. -c make -C bench/hsearch run   # build, flash, capture, report
```
