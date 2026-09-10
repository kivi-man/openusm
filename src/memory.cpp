#include "memory.h"

#include "debugutil.h"
#include "func_wrapper.h"
#include "os_developer_options.h"
#include "slab_allocator.h"
#include "utility.h"
#include "variable.h"

#include <cstdio>
#include <cstdlib>

Var<bool> mem_first_malloc{0x009224F0};

Var<bool> mem_first_memalign{0x009224F1};
Var<bool> mem_first_allocation{0x009224E8};

static Var<int> dword_965EC0{0x00965EC0};

int mem_set_checkpoint()
{
    return 0;
}

void mem_check_leaks_since_checkpoint(int, uint32_t)
{
  ;
}

#include <windows.h>
#include <new>

void *msvcr71_malloc(size_t size) {
    typedef void *(__cdecl *malloc_t)(size_t);
    static malloc_t s_malloc = nullptr;
    if (!s_malloc) {
        HMODULE h = GetModuleHandleA("msvcr71.dll");
        if (!h) h = LoadLibraryA("msvcr71.dll");
        if (h) s_malloc = (malloc_t)GetProcAddress(h, "malloc");
        if (!s_malloc) s_malloc = (malloc_t)0x00822076;
    }
    return s_malloc(size);
}

void msvcr71_free(void *p) {
    if (!p) return;
    typedef void (__cdecl *free_t)(void *);
    static free_t s_free = nullptr;
    if (!s_free) {
        HMODULE h = GetModuleHandleA("msvcr71.dll");
        if (!h) h = LoadLibraryA("msvcr71.dll");
        if (h) s_free = (free_t)GetProcAddress(h, "free");
        if (!s_free) s_free = (free_t)0x0082207c;
    }
    s_free(p);
}

size_t msvcr71_msize(void *p) {
    if (!p) return 0;
    typedef size_t (__cdecl *msize_t)(void *);
    static msize_t s_msize = nullptr;
    if (!s_msize) {
        HMODULE h = GetModuleHandleA("msvcr71.dll");
        if (!h) h = LoadLibraryA("msvcr71.dll");
        if (h) s_msize = (msize_t)GetProcAddress(h, "_msize");
        if (!s_msize) s_msize = (msize_t)0x0086F37C;
    }
    return s_msize ? s_msize(p) : 0;
}

void *operator new(size_t size) {
    void *p = msvcr71_malloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}

void *operator new[](size_t size) {
    void *p = msvcr71_malloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}

void operator delete(void *p) noexcept {
    msvcr71_free(p);
}

void operator delete[](void *p) noexcept {
    msvcr71_free(p);
}

void operator delete(void *p, size_t) noexcept {
    msvcr71_free(p);
}

void operator delete[](void *p, size_t) noexcept {
    msvcr71_free(p);
}

void *operator new(size_t size, const std::nothrow_t &) noexcept {
    return msvcr71_malloc(size);
}

void *operator new[](size_t size, const std::nothrow_t &) noexcept {
    return msvcr71_malloc(size);
}

void operator delete(void *p, const std::nothrow_t &) noexcept {
    msvcr71_free(p);
}

void operator delete[](void *p, const std::nothrow_t &) noexcept {
    msvcr71_free(p);
}

void *mem_alloc(size_t Size) {
    void *mem;

    if (slab_allocator::get_max_object_size() < Size) {
        mem = msvcr71_malloc(Size);
    } else {
        mem = slab_allocator::allocate(Size, nullptr);
    }

    return mem;
}

void mem_dealloc(void *a1, size_t Size) {
    if (Size <= slab_allocator::get_max_object_size()) {
        slab_allocator::deallocate(a1, nullptr);
    } else {
        msvcr71_free(a1);
    }
}

//0x0058EC30
void *arch_memalign_internal(size_t Alignment, size_t Size) {
    return bit_cast<void *>(CDECL_CALL(0x0058EC30, Alignment, Size));
}

void mem_on_first_allocation() {
    if (mem_first_allocation()) {
        debug_print_va("MEMTRACK is OFF");
        mem_print_stats("very first allocation");
        mem_first_allocation() = false;
    }
}

void *arch_memalign(size_t Alignment, size_t Size) {
    return (void *) CDECL_CALL(0x005357B0, Alignment, Size);
}

void mem_freealign(void *Memory) {
    if (Memory != nullptr) {
        CDECL_CALL(0x0058EC80, Memory);
    }
}

void mem_print_stats(const char *a1) {
    debug_print_va("mem_print_stats: %s\n", a1);
    debug_print_va("peak: %10lu   curr: %10lu   free: %10lu\n", 0ul, 0ul, 0ul);
}

void *arch_malloc(size_t Size) {
    if (mem_first_malloc()) {
        mem_on_first_allocation();

        mem_first_malloc() = false;
    }

    auto *mem = msvcr71_malloc(Size);
    dword_965EC0() += msvcr71_msize(mem);

    if (mem == nullptr) {
        debug_print_va("tried to allocate %d bytes", Size);
        mem_print_stats("mem_memalloc failed");

        os_developer_options().set_flag(mString{"ENABLE_LONG_CALLSTACK"}, false);
    }

    return mem;
}

void memory_patch() {
    REDIRECT(0x0059F684, arch_malloc);

    REDIRECT(0x00550E6B, arch_memalign_internal);
}
