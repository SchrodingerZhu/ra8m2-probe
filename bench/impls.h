#pragma once
#include <stddef.h>
#include <stdint.h>

typedef void  *(*cpy_fn)(void *, const void *, size_t);
typedef void  *(*set_fn)(void *, int, size_t);
typedef int    (*cmp_fn)(const void *, const void *, size_t);
typedef size_t (*len_fn)(const char *);
typedef void  *(*chr_fn)(const void *, int, size_t);

/* reference (byte loops, no vectorisation) */
void  *ref_memcpy(void *, const void *, size_t);
void  *ref_memmove(void *, const void *, size_t);
void  *ref_memset(void *, int, size_t);
int    ref_memcmp(const void *, const void *, size_t);
int    ref_bcmp(const void *, const void *, size_t);
size_t ref_strlen(const char *);
void  *ref_memchr(const void *, int, size_t);

/* plain C loops, compiler auto-vectorised (MVE tail-predicated) */
void  *av_memcpy(void *, const void *, size_t);
void  *av_memset(void *, int, size_t);

/* scalar 32-bit word implementations */
int    word_memcmp(const void *, const void *, size_t);
int    word_bcmp(const void *, const void *, size_t);
size_t word_strlen(const char *);
void  *word_memchr(const void *, int, size_t);

/* MVE (Helium) intrinsics */
void  *mve_memcpy(void *, const void *, size_t);        /* 16 B tail-predicated loop */
void  *mve64_memcpy(void *, const void *, size_t);      /* 64 B unrolled + predicated tail */
void  *mve_memmove(void *, const void *, size_t);
void  *mve_memset(void *, int, size_t);
void  *mve64_memset(void *, int, size_t);
int    mve_memcmp(const void *, const void *, size_t);  /* 16 B vcmp per block */
int    mve64_memcmp(const void *, const void *, size_t);/* 64 B xor/or batch */
int    mve_bcmp(const void *, const void *, size_t);
int    mve64_bcmp(const void *, const void *, size_t);
size_t mve_strlen(const char *);
size_t mve64_strlen(const char *);
void  *mve_memchr(const void *, int, size_t);

/* llvm-libc (tip of tree, compiled from source, symbols renamed) */
void  *llvm_memcpy(void *, const void *, size_t);
void  *llvm_memmove(void *, const void *, size_t);
void  *llvm_memset(void *, int, size_t);
int    llvm_memcmp(const void *, const void *, size_t);
int    llvm_bcmp(const void *, const void *, size_t);
size_t llvm_strlen(const char *);
void  *llvm_memchr(const void *, int, size_t);
size_t llvmw_strlen(const char *);                       /* LIBC_COPT_STRING_LENGTH_IMPL=word */
void  *llvmw_memchr(const void *, int, size_t);

/* picolibc (ATfE default C library for this multilib) */
void  *memcpy(void *, const void *, size_t);
void  *memmove(void *, const void *, size_t);
void  *memset(void *, int, size_t);
int    memcmp(const void *, const void *, size_t);
int    bcmp(const void *, const void *, size_t);
size_t strlen(const char *);
void  *memchr(const void *, int, size_t);
