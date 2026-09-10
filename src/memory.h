#pragma once

#include <variable.h>

extern Var<bool> mem_first_memalign;
extern Var<bool> mem_first_allocation;

void *arch_memalign_internal(size_t Alignment, size_t Size);

//0x005357B0
void *arch_memalign(size_t Alignment, size_t Size);

//0x0051CD10
void mem_print_stats(const char *a1);

int mem_set_checkpoint();

void mem_check_leaks_since_checkpoint(int, uint32_t);

//0x00535780
extern void *arch_malloc(size_t Size);

extern void memory_patch();

//0x0058EC80
extern void mem_freealign(void *Memory);

extern void *mem_alloc(size_t Size);

extern void mem_dealloc(void *a1, size_t Size);

#ifndef _MSVCR71_ALLOC_DECLARED
#define _MSVCR71_ALLOC_DECLARED
extern void *msvcr71_malloc(size_t size);

extern void msvcr71_free(void *p);

extern size_t msvcr71_msize(void *p);
#endif
