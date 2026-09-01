# llvm-libc string functions on Cortex-M85: can MVE (Helium) help?

Survey of `libc/src/string` in llvm-project tip of tree (`593d6710`, 2026-09-01) for
32-bit Arm, plus a cycle-accurate benchmark on the RA8M2 (Cortex-M85 r1p1) comparing
llvm-libc, picolibc, scalar-word and MVE implementations. Full tables: [RESULTS.md](RESULTS.md),
raw data: [results.csv](results.csv).

## 1. What llvm-libc does today on 32-bit Arm

| function | dispatch (`memory_utils/inline_*.h`) | codegen for `-mcpu=cortex-m85` |
|---|---|---|
| memcpy | `arm/inline_memcpy.h` → `inline_memcpy_arm_mid_end` | scalar, 64/16/4-byte word blocks via `ldr/str` (deliberately not LDRD/LDM), 286 B |
| memset | `arm/inline_memset.h` → `inline_memset_arm_mid_end` | scalar `str` blocks, 214 B |
| memmove | no Arm entry → `generic/byte_per_byte.h` | **byte loop** (memcpy only when non-overlapping) |
| memcmp | no Arm entry → `generic/byte_per_byte.h` | **byte loop**, 42 B |
| bcmp | no Arm entry → `generic/byte_per_byte.h` | **byte loop** |
| strlen | `string_length.h`, `LIBC_COPT_STRING_LENGTH_IMPL=element` default; `cpp::simd` width is 1 on Arm (`native_vector_size` only widens for SSE2/NEON) | **byte loop** |
| memchr | `find_first_character` `element` default | **byte loop** |

There is no MVE code anywhere in `libc/`. The only Arm-specific tuning is the
Cortex-M0..M52 word-copy in `arm/`, whose explicit `ldr/str` unrolling also prevents the
auto-vectoriser from doing anything. Note also that ATfE 22.1's own multilib for
`-mcpu=cortex-m85` is a `*_nomve_*` variant, so its prebuilt llvm-libc/picolibc are
MVE-free too.

Clang **does** auto-vectorise a plain `for (i) d[i] = s[i]` into a tail-predicated MVE loop
(`dlstp.8 / vldrb / vstrb / letp`) at `-O2` with `-fno-builtin`, but never early-exit loops
(strlen/memcmp/memchr), which is exactly where the byte loops live.

## 2. Benchmark

* Board: EK-RA8M2, Cortex-M85 r1p1, MVE-F, 16 KiB I + 16 KiB D cache, core clock ≈ 8.3 MHz
  (reset default, measured via SysTick from the host).
* Timing: `DWT_CYCCNT` around a call through a `volatile` function pointer, min of 15 runs,
  empty-call overhead subtracted (30 cycles with caches on, 71 with caches off).
* Inputs: random data, fresh per size; compare/search functions are worst case (difference /
  terminator at the last byte). Alignments: `dst+0/src+0`, `dst+1/src+1`, `dst+1/src+3`.
* Every implementation is checked against the byte reference on every size × alignment,
  including 64-byte canaries around `dst` — **0 failures**.
* Three phases: caches off + SRAM (code from flash, memory-bound); I+D cache on + SRAM;
  I+D on + buffers in DTCM. RTT and stack live in DTCM so the debugger never sees stale
  cached data.
* Toolchain: ATfE (clang 22.1.0), `-O2 -mcpu=cortex-m85 -mfloat-abi=hard`. llvm-libc compiled
  from the checked-out sources with the same flags (`llvm_*` symbols).

### Headline: I+D cache on, SRAM, aligned — cycles at n=1024 [cycles/byte] / n=16

| func | byte | picolibc | llvm-libc | scalar word | autovec | mve16 | mve64 | best vs llvm-libc |
|---|---|---|---|---|---|---|---|---|
| memcpy | 2183 [2.13] / 63 | 492 [0.48] / 41 | 463 [0.45] / 58 | – | 401 [0.39] / 23 | 401 [0.39] / 23 | **281 [0.27] / 33** | 1.6× |
| memcpy dst+1,src+3 | 2129 / 63 | 812 [0.79] / 74 | 809 [0.79] / 79 | – | 466 [0.46] / 24 | 466 / 24 | **398 [0.39] / 33** | 2.0× |
| memmove (overlap, bwd) | 2437 / 86 | 709 [0.69] / 67 | 5488 [5.36] / 112 | – | – | – | **312 [0.30] / 47** | 17.6× |
| memset | 1636 / 49 | 494 [0.48] / 52 | 239 [0.23] / 53 | – | 276 [0.27] / 23 | 276 / 23 | **195 [0.19] / 55** | 1.2× |
| memcmp | 5838 / 101 | 1594 [1.56] / 85 | 10639 [10.39] / 175 | 1737 [1.70] / 79 | – | 940 [0.92] / 36 | **543 [0.53] / 45** | 19.6× |
| bcmp | 5827 / 101 | 1587 [1.55] / 90 | 10624 [10.38] / 174 | 1750 [1.71] / 46 | – | 928 [0.91] / 28 | **497 [0.49] / 39** | 21.4× |
| strlen | 2426 / 59 | 2435 [2.38] / 68 | 2431 [2.37] / 53 | 1227 [1.20] / 61 (llvm `word`: 1846) | – | 619 [0.60] / 43 | **417 [0.41] / 37** | 5.8× |
| memchr | 5047 / 105 | 2313 [2.26] / 88 | 5100 [4.98] / 105 | 1757 [1.72] / 68 | – | **672 [0.66] / 34** | – | 7.6× |

Phase 0 (caches off, code fetched from flash) is ~3.5× slower across the board but ranks the
same; phase 2 (DTCM) is within a few % of phase 1 for everything except large copies
(`mve64` memcpy: 231 vs 281 cycles at 1 KiB, i.e. 4.4 B/cycle from TCM).

## 3. Findings

1. **memcmp / bcmp / memmove are the real problem, and it is not an MVE problem.** On Arm
   they fall through to byte loops that are 10 cycles/byte — *6.7× slower than picolibc's
   plain C* and 2× slower than even the naive byte reference (the `LIBC_LOOP_NOUNROLL` loop
   compiles to a load/load/cmp/bne with no unrolling). A scalar 32-bit word version
   (`impl_word.c`) is already 6×; MVE is 20×.
2. **MVE gives 1.6–2× on memcpy and 1.2× on memset** over llvm-libc's current arm code, and
   removes the misaligned-access penalty entirely (MVE loads/stores are alignment-agnostic:
   0.39 vs 0.79 cycles/byte at `dst+1,src+3`). Throughput tops out at ~3.7–4.4 B/cycle for
   copies and ~5.9 B/cycle for memset, consistent with the M85's 64-bit dual-beat MVE datapath.
3. **The compiler already produces the 16-byte tail-predicated loop for free** — `autovec`
   (plain C loop, `-fno-builtin`) equals the hand-written `mve16` cycle for cycle
   (`dlstp.8`/`letp`). Manual 64-byte unrolling with `vld1q/vst1q` adds another 30–40 % for
   n ≥ 128. Small sizes (1–16 B) are *faster* with a single predicated `vldrbt/vstrbt` than
   with the scalar byte/half/word switch (23 vs 54–58 cycles).
4. **Intrinsic loop shape matters.** The `for (i = 0; i < n; i += 16) { p = vctp8q(n - i); … }`
   form did not become a `dlstp/letp` loop (clang emitted `vctp` + two `vpst` per iteration)
   and was 2× slower than the canonical `while (n > 0) { p = vctp8q(n); …; n -= 16; }` form.
5. **strlen / memchr / memcmp with MVE are early-exit loops the vectoriser will never
   produce**, so they need explicit intrinsics: `vcmpeqq/vcmpneq` → 16-bit lane mask →
   `__builtin_ctz`. Batching 64 B with `vminq`/`veorq`+`vorrq` and a single cross-lane
   `vminvq`/`vmaxvq` per block avoids the `vmrs p0` serialisation and is another 1.5–1.7× at
   ≥ 256 B; for < 64 B the simple 16-byte version is slightly better.
6. Scalar-word strlen already in tree (`LIBC_COPT_STRING_LENGTH_IMPL=word`) is only 1.3×
   faster than the byte loop here (1846 vs 2431 cycles) because of its byte-wise
   prologue/epilogue; a tighter word version (`impl_word.c`) is 2×.

## 4. Suggested llvm-libc changes

Ordered by value ÷ risk:

1. **Add `arm/inline_memcmp.h` / `inline_bcmp.h` (scalar word) and route `memmove` to the
   existing arm memcpy blocks** for `__ARM_FEATURE_UNALIGNED` cores. Pure C, no new
   feature gates, 6–8× on every Cortex-M3+ — biggest win, zero risk.
2. **`#ifdef __ARM_FEATURE_MVE` paths** (`__ARM_FEATURE_MVE & 1` = integer MVE, which is all
   these need) for memcpy/memset/memmove/memcmp/bcmp/strlen/memchr using `<arm_mve.h>`:
   64-byte `vld1q/vst1q` blocks + one `vctp8q` predicated tail; `vcmpneq`/`vcmpeqq` masks for
   the search functions. Code size is small (`mve64_memcpy` = 126 B, `mve_memcmp` = 90 B).
3. Alternatively, for size-constrained builds, let the compiler do it: the tiny
   tail-predicated loop (`mve16`) is 32 B of code and already beats the current 286 B
   scalar memcpy at every size.

### Caveats that a libc must weigh before using MVE in `mem*`

* **FP/MVE context**: MVE uses the FP register file, so `memcpy` in an ISR or in an RTOS
  task now dirties the FP context. With lazy stacking (`FPCCR.LSPEN`, on by default) this
  costs nothing until an exception handler *also* touches FP/MVE, but an RTOS must save
  `S0–S31`/`Q0–Q7` on context switch for every task that ever calls `memcpy`. CP10/CP11 must
  be enabled (`CPACR`) before the first call — including from the startup code that copies
  `.data`, which is exactly where `memcpy` is first used.
* **TrustZone**: NS callers need `NSACR.CP10/CP11`; the secure side pays the full FP context
  save/restore on NS→S calls if MVE registers are live.
* These numbers are at 8 MHz with ~zero wait states. At 480 MHz, cache misses to AXI SRAM
  cost far more cycles, which hurts the instruction-heavy scalar versions more than the
  vector ones (fewer loads/stores per byte), so the ratios should widen, not shrink — but
  that is an expectation, not a measurement.
* Cortex-M55 has the same dual-beat MVE (results should transfer); Cortex-M52 is single-beat
  (halve the expected vector gains).

## Files

```
bench.c        harness: caches, DWT timing, verification, CSV over RTT
impl_mve.c     MVE intrinsics implementations (mve16 / mve64 variants)
impl_word.c    scalar 32-bit word memcmp/bcmp/strlen/memchr
ref.c / av.c   byte reference (unvectorised) / plain loops the compiler vectorises
link.ld        RTT + stack in DTCM, buffers in SRAM and DTCM
Makefile       builds llvm-libc tip sources from $LIBC_SRC and renames the entry points
rtt.py         pylink RTT reader;  clock.py  core-clock estimate via SysTick
report.py      results.csv -> RESULTS.md
```

Run: `nix develop -c make -C bench run` (flash, capture, report).
