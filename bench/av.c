/* Plain loops the compiler is free to vectorise (tail-predicated MVE). */
#include "impls.h"

void *av_memcpy(void *restrict d, const void *restrict s, size_t n)
{
    uint8_t *restrict dp = d; const uint8_t *restrict sp = s;
    for (size_t i = 0; i < n; i++) dp[i] = sp[i];
    return d;
}
void *av_memset(void *d, int c, size_t n)
{
    uint8_t *dp = d;
    for (size_t i = 0; i < n; i++) dp[i] = (uint8_t)c;
    return d;
}
