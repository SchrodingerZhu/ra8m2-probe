/* Armv8.1-M MVE (Helium) implementations using ACLE intrinsics.
 * 128-bit vectors, byte lanes; tails handled with VCTP predication so no
 * scalar clean-up loops are needed and nothing is read/written past `n`. */
#include <arm_mve.h>
#include "impls.h"

/* ---------------------------------------------------------------- memcpy */
void *mve_memcpy(void *restrict d, const void *restrict s, size_t n)
{
    uint8_t *restrict dp = d; const uint8_t *restrict sp = s;
    /* canonical tail-predicated form: clang turns this into dlstp.8/letp */
    while ((ptrdiff_t)n > 0) {
        mve_pred16_t p = vctp8q(n);
        vstrbq_p_u8(dp, vldrbq_z_u8(sp, p), p);
        dp += 16; sp += 16; n -= 16;
    }
    return d;
}

void *mve64_memcpy(void *restrict d, const void *restrict s, size_t n)
{
    uint8_t *restrict dp = d; const uint8_t *restrict sp = s;
    while (n >= 64) {
        uint8x16_t a = vld1q_u8(sp), b = vld1q_u8(sp + 16);
        uint8x16_t c = vld1q_u8(sp + 32), e = vld1q_u8(sp + 48);
        vst1q_u8(dp, a); vst1q_u8(dp + 16, b);
        vst1q_u8(dp + 32, c); vst1q_u8(dp + 48, e);
        dp += 64; sp += 64; n -= 64;
    }
    while (n >= 16) { vst1q_u8(dp, vld1q_u8(sp)); dp += 16; sp += 16; n -= 16; }
    if (n) {
        mve_pred16_t p = vctp8q(n);
        vstrbq_p_u8(dp, vldrbq_z_u8(sp, p), p);
    }
    return d;
}

/* --------------------------------------------------------------- memmove */
void *mve_memmove(void *d, const void *s, size_t n)
{
    uint8_t *dp = d; const uint8_t *sp = s;
    if (dp == sp || n == 0) return d;
    if (dp < sp || dp >= sp + n)                  /* no harmful overlap: forward */
        return mve64_memcpy(d, s, n);
    /* dst overlaps the tail of src: copy backwards, whole blocks first */
    while (n >= 64) {
        n -= 64;
        uint8x16_t a = vld1q_u8(sp + n), b = vld1q_u8(sp + n + 16);
        uint8x16_t c = vld1q_u8(sp + n + 32), e = vld1q_u8(sp + n + 48);
        vst1q_u8(dp + n, a); vst1q_u8(dp + n + 16, b);
        vst1q_u8(dp + n + 32, c); vst1q_u8(dp + n + 48, e);
    }
    while (n >= 16) { n -= 16; vst1q_u8(dp + n, vld1q_u8(sp + n)); }
    if (n) {                                      /* head: low lanes = first bytes */
        mve_pred16_t p = vctp8q(n);
        uint8x16_t v = vldrbq_z_u8(sp, p);
        vstrbq_p_u8(dp, v, p);
    }
    return d;
}

/* ---------------------------------------------------------------- memset */
void *mve_memset(void *d, int c, size_t n)
{
    uint8_t *dp = d;
    uint8x16_t v = vdupq_n_u8((uint8_t)c);
    while ((ptrdiff_t)n > 0) {
        vstrbq_p_u8(dp, v, vctp8q(n));
        dp += 16; n -= 16;
    }
    return d;
}

void *mve64_memset(void *d, int c, size_t n)
{
    uint8_t *dp = d;
    uint8x16_t v = vdupq_n_u8((uint8_t)c);
    while (n >= 64) {
        vst1q_u8(dp, v); vst1q_u8(dp + 16, v); vst1q_u8(dp + 32, v); vst1q_u8(dp + 48, v);
        dp += 64; n -= 64;
    }
    while (n >= 16) { vst1q_u8(dp, v); dp += 16; n -= 16; }
    if (n) vstrbq_p_u8(dp, v, vctp8q(n));
    return d;
}

/* ---------------------------------------------------------------- memcmp */
static inline int first_diff(const uint8_t *x, const uint8_t *y, mve_pred16_t ne)
{
    unsigned i = (unsigned)__builtin_ctz(ne);
    return (int)x[i] - (int)y[i];
}

int mve_memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    while (n >= 16) {
        mve_pred16_t ne = vcmpneq_u8(vld1q_u8(x), vld1q_u8(y));
        if (ne) return first_diff(x, y, ne);
        x += 16; y += 16; n -= 16;
    }
    if (n) {
        mve_pred16_t p = vctp8q(n);
        mve_pred16_t ne = vcmpneq_m_u8(vldrbq_z_u8(x, p), vldrbq_z_u8(y, p), p);
        if (ne) return first_diff(x, y, ne);
    }
    return 0;
}

/* Batch 64 B: xor/or accumulate in vector regs, one cross-lane reduction per
 * block, then locate the first differing byte only when a block mismatches. */
int mve64_memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    while (n >= 64) {
        uint8x16_t d0 = veorq_u8(vld1q_u8(x),      vld1q_u8(y));
        uint8x16_t d1 = veorq_u8(vld1q_u8(x + 16), vld1q_u8(y + 16));
        uint8x16_t d2 = veorq_u8(vld1q_u8(x + 32), vld1q_u8(y + 32));
        uint8x16_t d3 = veorq_u8(vld1q_u8(x + 48), vld1q_u8(y + 48));
        uint8x16_t acc = vorrq_u8(vorrq_u8(d0, d1), vorrq_u8(d2, d3));
        if (vmaxvq_u8(0, acc) != 0) break;       /* some byte differs: fall through to scan */
        x += 64; y += 64; n -= 64;
    }
    return mve_memcmp(x, y, n);
}

int mve_bcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    while (n >= 16) {
        if (vcmpneq_u8(vld1q_u8(x), vld1q_u8(y))) return 1;
        x += 16; y += 16; n -= 16;
    }
    if (n) {
        mve_pred16_t p = vctp8q(n);
        if (vcmpneq_m_u8(vldrbq_z_u8(x, p), vldrbq_z_u8(y, p), p)) return 1;
    }
    return 0;
}

int mve64_bcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    while (n >= 64) {
        uint8x16_t d0 = veorq_u8(vld1q_u8(x),      vld1q_u8(y));
        uint8x16_t d1 = veorq_u8(vld1q_u8(x + 16), vld1q_u8(y + 16));
        uint8x16_t d2 = veorq_u8(vld1q_u8(x + 32), vld1q_u8(y + 32));
        uint8x16_t d3 = veorq_u8(vld1q_u8(x + 48), vld1q_u8(y + 48));
        if (vmaxvq_u8(0, vorrq_u8(vorrq_u8(d0, d1), vorrq_u8(d2, d3))) != 0) return 1;
        x += 64; y += 64; n -= 64;
    }
    return mve_bcmp(x, y, n);
}

/* ---------------------------------------------------------------- strlen */
/* Reads are 16-byte aligned so they never cross into a page the string does
 * not touch (same trick as every SIMD strlen). */
size_t mve_strlen(const char *s)
{
    const uint8_t *p = (const uint8_t *)((uintptr_t)s & ~(uintptr_t)15);
    unsigned off = (unsigned)((uintptr_t)s & 15);
    mve_pred16_t m = vcmpeqq_n_u8(vld1q_u8(p), 0);
    m = (mve_pred16_t)(m >> off);
    if (m) return (size_t)__builtin_ctz(m);
    for (;;) {
        p += 16;
        m = vcmpeqq_n_u8(vld1q_u8(p), 0);
        if (m) return (size_t)(p - (const uint8_t *)s) + (size_t)__builtin_ctz(m);
    }
}

/* 64 B per iteration: vector min across four loads, one cross-lane VMINV. */
size_t mve64_strlen(const char *s)
{
    const uint8_t *p = (const uint8_t *)((uintptr_t)s & ~(uintptr_t)15);
    unsigned off = (unsigned)((uintptr_t)s & 15);
    mve_pred16_t m = vcmpeqq_n_u8(vld1q_u8(p), 0);
    m = (mve_pred16_t)(m >> off);
    if (m) return (size_t)__builtin_ctz(m);
    p += 16;
    /* align to 64 with single-vector steps */
    while ((uintptr_t)p & 63) {
        m = vcmpeqq_n_u8(vld1q_u8(p), 0);
        if (m) return (size_t)(p - (const uint8_t *)s) + (size_t)__builtin_ctz(m);
        p += 16;
    }
    for (;;) {
        uint8x16_t a = vld1q_u8(p), b = vld1q_u8(p + 16), c = vld1q_u8(p + 32), d = vld1q_u8(p + 48);
        uint8x16_t mn = vminq_u8(vminq_u8(a, b), vminq_u8(c, d));
        if (vminvq_u8(0xFF, mn) == 0) {
            if ((m = vcmpeqq_n_u8(a, 0))) return (size_t)(p - (const uint8_t *)s) + __builtin_ctz(m);
            if ((m = vcmpeqq_n_u8(b, 0))) return (size_t)(p + 16 - (const uint8_t *)s) + __builtin_ctz(m);
            if ((m = vcmpeqq_n_u8(c, 0))) return (size_t)(p + 32 - (const uint8_t *)s) + __builtin_ctz(m);
            m = vcmpeqq_n_u8(d, 0);
            return (size_t)(p + 48 - (const uint8_t *)s) + __builtin_ctz(m);
        }
        p += 64;
    }
}

/* ---------------------------------------------------------------- memchr */
void *mve_memchr(const void *s, int c, size_t n)
{
    const uint8_t *p = s; uint8_t cc = (uint8_t)c;
    while (n >= 16) {
        mve_pred16_t m = vcmpeqq_n_u8(vld1q_u8(p), cc);
        if (m) return (void *)(p + __builtin_ctz(m));
        p += 16; n -= 16;
    }
    if (n) {
        mve_pred16_t pr = vctp8q(n);
        mve_pred16_t m = vcmpeqq_m_n_u8(vldrbq_z_u8(p, pr), cc, pr);
        if (m) return (void *)(p + __builtin_ctz(m));
    }
    return 0;
}
