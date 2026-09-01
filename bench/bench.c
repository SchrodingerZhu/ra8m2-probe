/* String-function micro-benchmark for Cortex-M85 (RA8M2).
 * Cycle counts from DWT_CYCCNT, output as CSV over RTT (blocking).
 * Phases: 0 = caches off / SRAM, 1 = I+D cache on / SRAM, 2 = I+D on / DTCM. */
#include <stdint.h>
#include <stddef.h>
#include "rtt.h"
#include "impls.h"

#define REG(a) (*(volatile uint32_t *)(a))
#define DEMCR   REG(0xE000EDFC)
#define DWT_CTRL REG(0xE0001000)
#define DWT_CYCCNT REG(0xE0001004)
#define CCR     REG(0xE000ED14)
#define CSSELR  REG(0xE000ED84)
#define CCSIDR  REG(0xE000ED80)
#define ICIALLU REG(0xE000EF50)
#define DCISW   REG(0xE000EF60)
#define DCCISW  REG(0xE000EF74)

#define MAXN   16384
#define GUARD  64
#define BUFSZ  (MAXN + 2 * GUARD + 64)

__attribute__((section(".sram_buf"), aligned(64))) static uint8_t sram_a[BUFSZ], sram_b[BUFSZ], sram_c[BUFSZ];
__attribute__((section(".dtcm_buf"), aligned(64))) static uint8_t dtcm_a[BUFSZ], dtcm_b[BUFSZ];

static void out(const char *s) { while (*s) rtt_putc_block(*s++); }
static void out_u(uint32_t v) { char b[12]; int n = 0; do { b[n++] = '0' + v % 10; v /= 10; } while (v); while (n) rtt_putc_block(b[--n]); }
static void out_i(int v) { if (v < 0) { rtt_putc_block('-'); v = -v; } out_u((uint32_t)v); }

/* ---------------------------------------------------------------- caches */
static void dsb_isb(void) { __asm volatile("dsb\n isb" ::: "memory"); }

static void icache_enable(void) { ICIALLU = 0; dsb_isb(); CCR |= 1u << 17; dsb_isb(); }
static void icache_disable(void) { CCR &= ~(1u << 17); dsb_isb(); ICIALLU = 0; dsb_isb(); }

static void dcache_all(volatile uint32_t *op)
{
    CSSELR = 0; dsb_isb();
    uint32_t cc = CCSIDR;
    uint32_t sets = (cc >> 13) & 0x7FFF, ways = (cc >> 3) & 0x3FF;
    for (int s = (int)sets; s >= 0; s--)
        for (int w = (int)ways; w >= 0; w--)
            *op = ((uint32_t)s << 5) | ((uint32_t)w << 30);
    dsb_isb();
}
static void dcache_enable(void) { if (CCR & (1u << 16)) return; dcache_all(&DCISW); CCR |= 1u << 16; dsb_isb(); }
static void dcache_disable(void) { if (!(CCR & (1u << 16))) return; CCR &= ~(1u << 16); dsb_isb(); dcache_all(&DCCISW); }

/* ---------------------------------------------------------------- timing */
static inline uint32_t cyc(void) { uint32_t c; __asm volatile("isb" ::: "memory"); c = DWT_CYCCNT; __asm volatile("isb" ::: "memory"); return c; }

static uint32_t overhead;

__attribute__((noinline)) static void *nop_cpy(void *d, const void *s, size_t n) { (void)s; (void)n; return d; }

static uint32_t prng = 0x12345678;
static uint32_t rnd(void) { prng ^= prng << 13; prng ^= prng >> 17; prng ^= prng << 5; return prng; }

static void fill_random(uint8_t *p, size_t n) { for (size_t i = 0; i < n; i++) p[i] = (uint8_t)(rnd() >> 24); }

/* run `call` REPS times, return min and median cycles */
#define REPS 15
static uint32_t samples[REPS];
static void sort_samples(void)
{
    for (int i = 1; i < REPS; i++) { uint32_t v = samples[i]; int j = i - 1; while (j >= 0 && samples[j] > v) { samples[j + 1] = samples[j]; j--; } samples[j + 1] = v; }
}

typedef enum { F_MEMCPY, F_MEMMOVE_FWD, F_MEMMOVE_BWD, F_MEMSET, F_MEMCMP, F_BCMP, F_STRLEN, F_MEMCHR, F_COUNT } func_t;
static const char *func_name[F_COUNT] = { "memcpy", "memmove_fwd", "memmove_bwd", "memset", "memcmp", "bcmp", "strlen", "memchr" };

typedef struct { const char *name; void *fn; } impl_t;

static const impl_t impls_memcpy[] = {
    { "byte", ref_memcpy }, { "autovec", av_memcpy }, { "picolibc", memcpy }, { "llvm-libc", llvm_memcpy },
    { "mve16", mve_memcpy }, { "mve64", mve64_memcpy }, { 0, 0 } };
static const impl_t impls_memmove[] = {
    { "byte", ref_memmove }, { "picolibc", memmove }, { "llvm-libc", llvm_memmove }, { "mve64", mve_memmove }, { 0, 0 } };
static const impl_t impls_memset[] = {
    { "byte", ref_memset }, { "autovec", av_memset }, { "picolibc", memset }, { "llvm-libc", llvm_memset },
    { "mve16", mve_memset }, { "mve64", mve64_memset }, { 0, 0 } };
static const impl_t impls_memcmp[] = {
    { "byte", ref_memcmp }, { "picolibc", memcmp }, { "llvm-libc", llvm_memcmp }, { "word", word_memcmp },
    { "mve16", mve_memcmp }, { "mve64", mve64_memcmp }, { 0, 0 } };
static const impl_t impls_bcmp[] = {
    { "byte", ref_bcmp }, { "picolibc", bcmp }, { "llvm-libc", llvm_bcmp }, { "word", word_bcmp },
    { "mve16", mve_bcmp }, { "mve64", mve64_bcmp }, { 0, 0 } };
static const impl_t impls_strlen[] = {
    { "byte", ref_strlen }, { "picolibc", strlen }, { "llvm-libc", llvm_strlen }, { "llvm-word", llvmw_strlen },
    { "word", word_strlen }, { "mve16", mve_strlen }, { "mve64", mve64_strlen }, { 0, 0 } };
static const impl_t impls_memchr[] = {
    { "byte", ref_memchr }, { "picolibc", memchr }, { "llvm-libc", llvm_memchr }, { "llvm-word", llvmw_memchr },
    { "word", word_memchr }, { "mve16", mve_memchr }, { 0, 0 } };

static const impl_t *impls_for(func_t f)
{
    switch (f) {
    case F_MEMCPY: return impls_memcpy;
    case F_MEMMOVE_FWD: case F_MEMMOVE_BWD: return impls_memmove;
    case F_MEMSET: return impls_memset;
    case F_MEMCMP: return impls_memcmp;
    case F_BCMP: return impls_bcmp;
    case F_STRLEN: return impls_strlen;
    case F_MEMCHR: return impls_memchr;
    default: return 0;
    }
}

static const uint32_t sizes[] = { 1, 2, 3, 4, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65, 100, 128, 255, 256,
                                  512, 1000, 1024, 4096, 8192, 16384 };
#define NSIZES (sizeof sizes / sizeof sizes[0])

/* alignment pairs (dst offset, src offset) */
static const uint8_t aligns2[][2] = { {0, 0}, {1, 1}, {1, 3} };
static const uint8_t aligns1[][2] = { {0, 0}, {1, 0} };

static uint32_t verify_fail;

/* set up inputs for one measurement; returns 0 on success */
static void prepare(func_t f, uint8_t *A, uint8_t *B, size_t n)
{
    switch (f) {
    case F_MEMCPY: case F_MEMMOVE_FWD: case F_MEMMOVE_BWD:
        fill_random(B, n + 64); fill_random(A - GUARD, n + 2 * GUARD); break;
    case F_MEMSET:
        fill_random(A - GUARD, n + 2 * GUARD); break;
    case F_MEMCMP: case F_BCMP:
        fill_random(A, n); for (size_t i = 0; i < n; i++) B[i] = A[i];
        B[n - 1] = (uint8_t)(A[n - 1] + 1 + (rnd() & 0x7F));   /* differ only at the last byte */
        break;
    case F_STRLEN:
        for (size_t i = 0; i < n; i++) A[i] = (uint8_t)(1 + (rnd() % 255));
        A[n] = 0; A[n + 1] = 'x'; break;
    case F_MEMCHR:
        for (size_t i = 0; i < n; i++) A[i] = (uint8_t)(rnd() % 255);   /* never 0xFF */
        A[n - 1] = 0xFF; A[n] = 0xFF; break;
    default: break;
    }
}

/* execute the function once; for copy/set, `A` is dst and `B` src */
static inline int run_once(func_t f, void *fn, uint8_t *A, uint8_t *B, size_t n, int *ires, size_t *sres)
{
    switch (f) {
    case F_MEMCPY: ((cpy_fn)fn)(A, B, n); return 0;
    case F_MEMMOVE_FWD: ((cpy_fn)fn)(A, A + 8, n); return 0;       /* dst < src, overlapping */
    case F_MEMMOVE_BWD: ((cpy_fn)fn)(A + 8, A, n); return 0;       /* dst > src, overlapping */
    case F_MEMSET: ((set_fn)fn)(A, 0x5A, n); return 0;
    case F_MEMCMP: *ires = ((cmp_fn)fn)(A, B, n); return 0;
    case F_BCMP: *ires = ((cmp_fn)fn)(A, B, n); return 0;
    case F_STRLEN: *sres = ((len_fn)fn)((const char *)A); return 0;
    case F_MEMCHR: *sres = (size_t)((uint8_t *)((chr_fn)fn)(A, 0xFF, n) - A); return 0;
    default: return 0;
    }
}

/* correctness: compare against the reference on the same inputs */
static int verify(func_t f, void *fn, uint8_t *A, uint8_t *B, uint8_t *C, size_t n)
{
    prepare(f, A, B, n);
    /* snapshot A region (incl. guards) into C */
    for (size_t i = 0; i < n + 2 * GUARD; i++) C[i] = A[i - GUARD];
    int ir = 0, rr = 0; size_t sr = 0, rs = 0;
    run_once(f, fn, A, B, n, &ir, &sr);
    /* reference on the snapshot */
    uint8_t *RA = C + GUARD;
    switch (f) {
    case F_MEMCPY: ref_memcpy(RA, B, n); break;
    case F_MEMMOVE_FWD: ref_memmove(RA, RA + 8, n); break;
    case F_MEMMOVE_BWD: ref_memmove(RA + 8, RA, n); break;
    case F_MEMSET: ref_memset(RA, 0x5A, n); break;
    case F_MEMCMP: rr = ref_memcmp(A, B, n); break;
    case F_BCMP: rr = ref_bcmp(A, B, n); break;
    case F_STRLEN: rs = ref_strlen((const char *)A); break;
    case F_MEMCHR: rs = (size_t)((uint8_t *)ref_memchr(A, 0xFF, n) - A); break;
    default: break;
    }
    int bad = 0;
    if (f <= F_MEMSET) {
        for (size_t i = 0; i < n + 2 * GUARD; i++) if (A[i - GUARD] != C[i]) { bad = 1; break; }
    } else if (f == F_MEMCMP) {
        bad = (ir < 0) != (rr < 0) || (ir == 0) != (rr == 0);
    } else if (f == F_BCMP) {
        bad = (ir != 0) != (rr != 0);
    } else bad = sr != rs;
    return bad;
}

static void bench_one(int phase, func_t f, const impl_t *im, uint8_t *base_a, uint8_t *base_b, uint8_t *scratch,
                      size_t n, unsigned da, unsigned sa)
{
    uint8_t *A = base_a + GUARD + da, *B = base_b + GUARD + sa;
    /* correctness, on several random inputs */
    int bad = 0;
    for (int k = 0; k < 3 && !bad; k++) bad = verify(f, im->fn, A, B, scratch, n);
    if (bad) {
        verify_fail++;
        out("V,"); out_u(phase); out(","); out(func_name[f]); out(","); out(im->name); out(","); out_u(n);
        out(","); out_u(da); out(","); out_u(sa); out(",FAIL\n");
        return;
    }
    prepare(f, A, B, n);
    int ir; size_t sr;
    for (int r = 0; r < REPS; r++) {
        uint32_t t0 = cyc();
        run_once(f, im->fn, A, B, n, &ir, &sr);
        uint32_t t1 = cyc();
        samples[r] = t1 - t0;
    }
    sort_samples();
    uint32_t mn = samples[0] > overhead ? samples[0] - overhead : 0;
    uint32_t md = samples[REPS / 2] > overhead ? samples[REPS / 2] - overhead : 0;
    out("R,"); out_u(phase); out(","); out(func_name[f]); out(","); out(im->name); out(","); out_u(n);
    out(","); out_u(da); out(","); out_u(sa); out(","); out_u(mn); out(","); out_u(md); out("\n");
}

static void calibrate(void)
{
    volatile cpy_fn fp = nop_cpy;
    uint8_t d[4], s[4];
    for (int r = 0; r < REPS; r++) { uint32_t t0 = cyc(); fp(d, s, 4); uint32_t t1 = cyc(); samples[r] = t1 - t0; }
    sort_samples();
    overhead = samples[0];
}

static void run_phase(int phase)
{
    uint8_t *ba, *bb, *sc;
    if (phase == 2) { ba = dtcm_a; bb = dtcm_b; sc = sram_c; }
    else { ba = sram_a; bb = sram_b; sc = sram_c; }
    if (phase == 0) { dcache_disable(); icache_disable(); }
    else { icache_enable(); dcache_enable(); }
    calibrate();
    out("# phase "); out_u(phase); out(" CCR=0x"); { uint32_t c = CCR; char h[9]; for (int i = 7; i >= 0; i--) { h[i] = "0123456789ABCDEF"[c & 15]; c >>= 4; } h[8] = 0; out(h); }
    out(" overhead="); out_u(overhead); out("\n");

    for (func_t f = 0; f < F_COUNT; f++) {
        const impl_t *ims = impls_for(f);
        int two = (f == F_MEMCPY || f == F_MEMCMP || f == F_BCMP);
        for (const impl_t *im = ims; im->name; im++) {
            for (unsigned si = 0; si < NSIZES; si++) {
                size_t n = sizes[si];
                if (two) { for (unsigned a = 0; a < 3; a++) bench_one(phase, f, im, ba, bb, sc, n, aligns2[a][0], aligns2[a][1]); }
                else     { for (unsigned a = 0; a < 2; a++) bench_one(phase, f, im, ba, bb, sc, n, aligns1[a][0], aligns1[a][1]); }
            }
        }
    }
}

int main(void)
{
    rtt_init();
    DEMCR |= 1u << 24;                       /* TRCENA */
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1u;                          /* CYCCNTENA */

    out("# cortex-m85 string bench; CPUID=0x"); { uint32_t c = REG(0xE000ED00); char h[9]; for (int i = 7; i >= 0; i--) { h[i] = "0123456789ABCDEF"[c & 15]; c >>= 4; } h[8] = 0; out(h); }
    out(" REPS="); out_u(REPS); out("\n");
    out("# columns: R,phase,func,impl,n,dst_off,src_off,min_cycles,median_cycles\n");
    for (int ph = 0; ph < 3; ph++) run_phase(ph);
    out("# verify_failures="); out_u(verify_fail); out("\nEND\n");
    for (;;) __asm volatile("nop");          /* busy: keep CYCCNT running for clock.py */
}
