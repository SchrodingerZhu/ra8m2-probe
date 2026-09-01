/* hsearch benchmark for Cortex-M85 (RA8M2): llvm-libc's SwissTable-style
 * hash table (4 variants). Same hash in every
 * variant; only the group/probing structure differs.
 * DWT_CYCCNT timing, CSV over RTT (blocking). Caches (I+D) enabled. */
#include <stdint.h>
#include <stddef.h>
#include <search.h>
#include <string.h>
#include "../rtt.h"

#define REG(a) (*(volatile uint32_t *)(a))

/* ---- variant APIs (see llvm_hsearch.cpp) ---- */
#define DECL_VARIANT(P) \
    void *P##_create(size_t, uint64_t); \
    ENTRY *P##_insert(void **, ENTRY); \
    ENTRY *P##_find(void *, const char *); \
    void P##_destroy(void *); \
    uint64_t P##_hash(uint64_t, const char *);
DECL_VARIANT(lh)    /* llvm-libc as-is: generic 4B SWAR group */
DECL_VARIANT(lh8)   /* 8B SWAR group (portable) */
DECL_VARIANT(lhm)   /* 16B MVE group */
DECL_VARIANT(lhc)   /* 4B group, cheap instrumentation hash */
DECL_VARIANT(lh8c)  /* 8B group, cheap hash */
DECL_VARIANT(lhmc)  /* 16B MVE group, cheap hash */

extern size_t hs_alloc_live, hs_alloc_peak, hs_alloc_count;
void hs_alloc_reset(void);

/* ---- output ---- */
static void out(const char *s) { while (*s) rtt_putc_block(*s++); }
static void out_u(uint32_t v) { char b[12]; int n = 0; do { b[n++] = '0' + v % 10; v /= 10; } while (v); while (n) rtt_putc_block(b[--n]); }

/* ---- timing ---- */
static inline uint32_t cyc(void) { uint32_t c; __asm volatile("isb" ::: "memory"); c = REG(0xE0001004); __asm volatile("isb" ::: "memory"); return c; }
static void dsb_isb(void) { __asm volatile("dsb\n isb" ::: "memory"); }

static void caches_on(void)
{
    REG(0xE000EF50) = 0; dsb_isb(); REG(0xE000ED14) |= 1u << 17; dsb_isb();  /* I */
    REG(0xE000ED84) = 0; dsb_isb();
    uint32_t cc = REG(0xE000ED80);
    uint32_t sets = (cc >> 13) & 0x7FFF, ways = (cc >> 3) & 0x3FF;
    for (int s = (int)sets; s >= 0; s--)
        for (int w = (int)ways; w >= 0; w--)
            REG(0xE000EF60) = ((uint32_t)s << 5) | ((uint32_t)w << 30);
    dsb_isb(); REG(0xE000ED14) |= 1u << 16; dsb_isb();                        /* D */
}

/* ---- keys ---- */
#define MAXN 4096
#define KEYSPACE (MAXN * 2)
#define KEYLEN_MAX 24
__attribute__((section(".sram_buf"))) static char keys[KEYSPACE][KEYLEN_MAX];
static const char *key(size_t i) { return keys[i]; }

static uint32_t prng = 0xC0FFEE42;
static uint32_t rnd(void) { prng ^= prng << 13; prng ^= prng >> 17; prng ^= prng << 5; return prng; }

static void make_keys(void)
{
    static const char alpha[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
    for (size_t i = 0; i < KEYSPACE; i++) {
        unsigned len = 4 + rnd() % 16;                    /* 4..19 chars */
        for (unsigned j = 0; j < len; j++) keys[i][j] = alpha[rnd() % 63];
        keys[i][len] = 0;
        /* uniqueness by suffix */
        keys[i][0] = alpha[i % 63];
        keys[i][1] = alpha[(i / 63) % 63];
        keys[i][2] = alpha[(i / (63 * 63)) % 63];
    }
}

static uint16_t order[MAXN];
static void shuffle_order(size_t n)
{
    for (size_t i = 0; i < n; i++) order[i] = (uint16_t)i;
    for (size_t i = n - 1; i > 0; i--) { size_t j = rnd() % (i + 1); uint16_t t = order[i]; order[i] = order[j]; order[j] = t; }
}

typedef struct {
    const char *name;
    void *(*create)(size_t, uint64_t);
    ENTRY *(*insert)(void **, ENTRY);
    ENTRY *(*find)(void *, const char *);
    void (*destroy)(void *);
    uint64_t (*hash)(uint64_t, const char *);
} variant_t;

#define V(P, N) { N, P##_create, P##_insert, P##_find, P##_destroy, P##_hash }
static const variant_t variants[] = {
    V(lh,  "llvm"),
    V(lh8, "llvm+grp8"),
    V(lhm, "llvm+mveGroup"),
    V(lhc, "fnv1a/grp4"),
    V(lh8c, "fnv1a/grp8"),
    V(lhmc, "fnv1a/grp16mve"),
};
#define NVARIANTS (sizeof variants / sizeof variants[0])

static const uint32_t table_sizes[] = { 16, 64, 256, 1024, 4096 };
#define NTS (sizeof table_sizes / sizeof table_sizes[0])
/* exact 7/8 fill of a power-of-two table: the state right before a resize,
 * where probe runs are longest and group width matters most */
static const uint32_t hiload_sizes[] = { 14, 56, 224, 896, 3584 };
#define SEED 0x2026090100000001ull

static uint32_t fails;

static void row(const char *impl, const char *op, uint32_t n, uint32_t total, uint32_t ops, uint32_t mem)
{
    out("R,"); out(impl); out(","); out(op); out(","); out_u(n); out(",");
    out_u(total); out(","); out_u(ops); out(","); out_u(mem); out("\n");
}

/* run one llvm-variant at size n; opsuffix distinguishes load levels */
static void bench_variant2(const variant_t *v, size_t n, const char *sfx)
{
    uint32_t t0, t1;

    /* --- presized insert --- */
    hs_alloc_reset();
    void *t = v->create(n, SEED);
    if (!t) { out("V,"); out(v->name); out(",create,FAIL\n"); fails++; return; }
    shuffle_order(n);
    t0 = cyc();
    for (size_t i = 0; i < n; i++) {
        ENTRY e = { (char *)key(order[i]), (void *)(uintptr_t)(order[i] + 1) };
        if (!v->insert(&t, e)) { fails++; }
    }
    t1 = cyc();
    { char op[24]; unsigned k = 0; const char *p = "insert"; while (*p) op[k++] = *p++; p = sfx; while (*p) op[k++] = *p++; op[k] = 0;
      row(v->name, op, n, t1 - t0, n, (uint32_t)hs_alloc_peak); }

    /* --- verify + find hit (3 passes, take min) --- */
    uint32_t best = 0xFFFFFFFF;
    for (int pass = 0; pass < 3; pass++) {
        shuffle_order(n);
        t0 = cyc();
        for (size_t i = 0; i < n; i++) {
            ENTRY *r = v->find(t, key(order[i]));
            if (!r || r->data != (void *)(uintptr_t)(order[i] + 1)) fails++;
        }
        t1 = cyc();
        if (t1 - t0 < best) best = t1 - t0;
    }
    { char op[24]; unsigned k = 0; const char *p = "find_hit"; while (*p) op[k++] = *p++; p = sfx; while (*p) op[k++] = *p++; op[k] = 0;
      row(v->name, op, n, best, n, 0); }

    /* --- find miss --- */
    best = 0xFFFFFFFF;
    for (int pass = 0; pass < 3; pass++) {
        t0 = cyc();
        for (size_t i = 0; i < n; i++) {
            if (v->find(t, key(MAXN + i)) != 0) fails++;
        }
        t1 = cyc();
        if (t1 - t0 < best) best = t1 - t0;
    }
    { char op[24]; unsigned k = 0; const char *p = "find_miss"; while (*p) op[k++] = *p++; p = sfx; while (*p) op[k++] = *p++; op[k] = 0;
      row(v->name, op, n, best, n, 0); }
    v->destroy(t);
    if (sfx[0]) return;   /* growth run only for the standard-load series */

    /* --- grow-from-empty insert (amortized growth cost) --- */
    hs_alloc_reset();
    t = v->create(0, SEED);
    shuffle_order(n);
    t0 = cyc();
    for (size_t i = 0; i < n; i++) {
        ENTRY e = { (char *)key(order[i]), (void *)(uintptr_t)(order[i] + 1) };
        if (!v->insert(&t, e)) fails++;
    }
    t1 = cyc();
    row(v->name, "insert_grow", n, t1 - t0, n, (uint32_t)hs_alloc_peak);
    v->destroy(t);
}

/* hash-function microbenchmark: how much of a lookup is just the hash */
static void bench_hash(void)
{
    for (unsigned vi = 0; vi < NVARIANTS; vi += 3) {   /* lh (aHash64) and lhc (cheap) */
        const variant_t *v = &variants[vi];
        volatile uint64_t sink = 0;
        uint32_t t0 = cyc();
        for (size_t i = 0; i < 1024; i++) sink += v->hash(SEED, key(i & (MAXN - 1)));
        uint32_t t1 = cyc();
        row(v->name, "hash_only", 1024, t1 - t0, 1024, 0);
        (void)sink;
    }
}

int main(void)
{
    rtt_init();
    REG(0xE000EDFC) |= 1u << 24;  /* TRCENA */
    REG(0xE0001004) = 0;
    REG(0xE0001000) |= 1u;        /* CYCCNTENA */
    caches_on();
    make_keys();

    out("# hsearch bench, Cortex-M85, I+D cache on, keys 4-19 chars\n");
    out("# columns: R,impl,op,n,total_cycles,ops,peak_alloc_bytes\n");
    for (unsigned s = 0; s < NTS; s++) {
        size_t n = table_sizes[s];
        for (unsigned vi = 0; vi < NVARIANTS; vi++) bench_variant2(&variants[vi], n, "");
    }
    for (unsigned s = 0; s < NTS; s++)
        for (unsigned vi = 0; vi < NVARIANTS; vi++)
            bench_variant2(&variants[vi], hiload_sizes[s], "@7/8");
    bench_hash();
    out("# fails="); out_u(fails); out("\nEND\n");
    for (;;) __asm volatile("nop");
}
