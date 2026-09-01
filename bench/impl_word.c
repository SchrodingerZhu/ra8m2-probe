/* Scalar 32-bit-word implementations for cores with unaligned access
 * (what a non-MVE llvm-libc patch for Armv7-M+ would look like). */
#include "impls.h"

typedef uint32_t __attribute__((aligned(1), may_alias)) u32u;

static inline uint32_t ld(const uint8_t *p) { return *(const u32u *)p; }

int word_memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    while (n >= 16) {
        uint32_t a0 = ld(x), b0 = ld(y), a1 = ld(x + 4), b1 = ld(y + 4);
        uint32_t a2 = ld(x + 8), b2 = ld(y + 8), a3 = ld(x + 12), b3 = ld(y + 12);
        if (((a0 ^ b0) | (a1 ^ b1) | (a2 ^ b2) | (a3 ^ b3)) != 0) break;
        x += 16; y += 16; n -= 16;
    }
    while (n >= 4) {
        uint32_t u = ld(x), v = ld(y);
        if (u != v) {
            unsigned i = (unsigned)__builtin_ctz(u ^ v) >> 3;   /* little-endian */
            return (int)x[i] - (int)y[i];
        }
        x += 4; y += 4; n -= 4;
    }
    for (; n; n--, x++, y++)
        if (*x != *y) return (int)*x - (int)*y;
    return 0;
}

int word_bcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    uint32_t acc = 0;
    while (n >= 16) {
        acc = (ld(x) ^ ld(y)) | (ld(x + 4) ^ ld(y + 4)) | (ld(x + 8) ^ ld(y + 8)) | (ld(x + 12) ^ ld(y + 12));
        if (acc) return 1;
        x += 16; y += 16; n -= 16;
    }
    while (n >= 4) { if (ld(x) != ld(y)) return 1; x += 4; y += 4; n -= 4; }
    for (; n; n--, x++, y++) if (*x != *y) return 1;
    return 0;
}

static inline uint32_t has_zero(uint32_t w) { return (w - 0x01010101u) & ~w & 0x80808080u; }

size_t word_strlen(const char *s)
{
    const uint8_t *p = (const uint8_t *)s;
    while ((uintptr_t)p & 3) { if (!*p) return (size_t)((const char *)p - s); p++; }
    const uint32_t *w = (const uint32_t *)p;
    for (;;) {
        uint32_t v = *w;
        if (has_zero(v)) {
            p = (const uint8_t *)w;
            while (*p) p++;
            return (size_t)((const char *)p - s);
        }
        w++;
    }
}

void *word_memchr(const void *s, int c, size_t n)
{
    const uint8_t *p = s; uint8_t cc = (uint8_t)c;
    uint32_t mask = 0x01010101u * cc;
    while (n >= 4) {
        uint32_t v = ld(p) ^ mask;
        if (has_zero(v)) break;
        p += 4; n -= 4;
    }
    for (; n; n--, p++) if (*p == cc) return (void *)p;
    return 0;
}
