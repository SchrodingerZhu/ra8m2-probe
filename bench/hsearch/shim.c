/* Freestanding glue for the standalone llvm-libc hash table:
 *  - sbrk over a static SRAM arena (for picolibc malloc)
 *  - aligned_alloc/free pair (llvm-libc allocates via ::aligned_alloc and
 *    frees via the renamed operator delete symbol __llvm_libc_delete_aligned)
 *  - allocation statistics for the report                                    */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ARENA_SIZE (768 * 1024)
__attribute__((section(".sram_buf"), aligned(64))) static uint8_t arena[ARENA_SIZE];
static size_t brk_off;

void *sbrk(ptrdiff_t inc)
{
    if (brk_off + (size_t)inc > ARENA_SIZE) return (void *)-1;
    void *p = &arena[brk_off];
    brk_off += (size_t)inc;
    return p;
}
void *_sbrk(ptrdiff_t inc) { return sbrk(inc); }

/* --- allocation statistics ------------------------------------------- */
size_t hs_alloc_live, hs_alloc_peak, hs_alloc_count;
static void stat_add(size_t n) { hs_alloc_live += n; hs_alloc_count++; if (hs_alloc_live > hs_alloc_peak) hs_alloc_peak = hs_alloc_live; }
static void stat_sub(size_t n) { hs_alloc_live -= n; }
void hs_alloc_reset(void) { hs_alloc_live = hs_alloc_peak = hs_alloc_count = 0; }

/* --- aligned_alloc over malloc --------------------------------------- */
void *malloc(size_t);
void free(void *);

typedef struct { void *base; size_t size; } hdr_t;

void *aligned_alloc(size_t align, size_t size)
{
    if (align < sizeof(void *)) align = sizeof(void *);
    void *base = malloc(size + align + sizeof(hdr_t));
    if (!base) return 0;
    uintptr_t user = ((uintptr_t)base + sizeof(hdr_t) + align - 1) & ~(uintptr_t)(align - 1);
    hdr_t *h = (hdr_t *)(user - sizeof(hdr_t));
    h->base = base;
    h->size = size;
    stat_add(size);
    return (void *)user;
}

static void aligned_free(void *p)
{
    if (!p) return;
    hdr_t *h = (hdr_t *)((uintptr_t)p - sizeof(hdr_t));
    stat_sub(h->size);
    free(h->base);
}

/* llvm-libc's operator delete(void*, std::align_val_t), renamed by
 * DELETE_NAME to "<LIBC_NAMESPACE>_delete_aligned" -- one namespace per
 * variant TU (distinct namespaces prevent cross-variant weak-symbol dedup). */
#define DELETE_SHIMS(ns)     void ns##_delete_aligned(void *p, size_t a) { (void)a; aligned_free(p); }     void ns##_delete(void *p) { aligned_free(p); }
DELETE_SHIMS(__llvm_lh)
DELETE_SHIMS(__llvm_lh8)
DELETE_SHIMS(__llvm_lhm)
DELETE_SHIMS(__llvm_lhc)
DELETE_SHIMS(__llvm_lh8c)
DELETE_SHIMS(__llvm_lhmc)
