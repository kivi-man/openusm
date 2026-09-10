#include "slab_allocator.h"

#include "common.h"
#include "debugutil.h"
#include "debug_menu.h"
#include "func_wrapper.h"
#include "log.h"
#include "memory.h"
#include "os_developer_options.h"
#include "trace.h"
#include "utility.h"
#include "variable.h"

#include <cassert>
#include <windows.h>

static constexpr auto SLAB_HEADER_BEGIN_SENTRY = 0xE1E1E1E1;
static constexpr auto SLAB_HEADER_END_SENTRY = 0x1E1E1E1E;

static constexpr auto SLAB_FROM_STATIC = 0x10;

static constexpr auto SLAB_FROM_HEAP = 0x8;

static constexpr auto SLAB_FROM_AUX = 0x20;

static constexpr auto SLAB_ON_PARTIAL_LIST = 0x1;

static constexpr auto SLAB_ON_FULL_LIST = 0x2;

static constexpr auto SLAB_ON_FREE_LIST = 0x4;

static constexpr auto MAX_OBJECT_SIZE = 176;

namespace slab_allocator {
VALIDATE_SIZE(slab_t, 0x24);
VALIDATE_OFFSET(slab_t, end_sentry, 0x20);

VALIDATE_SIZE(slab_list_t, 0xC);
VALIDATE_SIZE(slab_partial_list_t, 0x214);

int allocated_object_count[44]{};
int free_object_count[44]{};

int partial_slab_count[44]{};
int full_slab_count[44]{};

int free_slab_count{};
int total_slab_count{};

#if !STANDALONE_SYSTEM

static auto & initialized = var<bool>(0x00965F33);

static auto & g_dump_slab_info = var<bool>(0x00965F40);

static auto & static_slab_arena = var<char *>(0x00965F34);

static auto & static_slab_headers = var<slab_t *>(0x00965F38);

static auto & slab_partial_list = var<slab_list_t *>(0x00965984);

static auto & slab_full_list = var<slab_list_t *>(0x00965980);

static auto & slab_free_list = var<slab_list_t *>(0x0096597C);
#else

#define make_var(type, name) \
    static type g_##name {}; \
    type & name {g_##name}

make_var(bool, initialized);

make_var(bool, g_dump_slab_info);

make_var(char *, static_slab_arena); 

make_var(slab_t *, static_slab_headers); 

make_var(slab_list_t *, slab_partial_list); 

make_var(slab_list_t *, slab_full_list); 

make_var(slab_list_t *, slab_free_list); 

#undef make_var
#endif

}

slab_allocator::slab_t *slab_allocator::slab_t::iterator::operator*() {
    return this->_ptr;
}

slab_allocator::slab_t::iterator slab_allocator::slab_t::iterator::operator++() {
    if (this->_ptr != nullptr) {
        this->_ptr = this->_ptr->simple_list_vars._sl_next_element;
    }

    return (*this);
}

slab_allocator::slab_t::iterator slab_allocator::slab_t::iterator::operator++(
    [[maybe_unused]] int a3) {
    slab_t::iterator a1{this->_ptr};
    if (this->_ptr != nullptr) {
        this->_ptr = this->_ptr->simple_list_vars._sl_next_element;
    }

    return a1;
}

bool slab_allocator::slab_t::iterator::operator!=(const slab_t::iterator &a2) {
    return this->_ptr != a2._ptr;
}

bool slab_allocator::slab_t::iterator::operator==(const slab_t::iterator &a2) {
    return !(this->_ptr != a2._ptr);
}

void slab_allocator::initialize()
{
    assert(!initialized);

    if constexpr (1)
    {
        slab_list_t *v1 = nullptr;
        auto *v0 = (slab_partial_list_t *) operator new(0x214u);
        if (v0 != nullptr)
        {
            v1 = v0->field_4;
            v0->field_0 = 44;
            new (v0->field_4) slab_list_t[44];
        }

        slab_partial_list = v1;

        slab_list_t *v3 = nullptr;
        auto *v2 = (slab_partial_list_t *) operator new(0x214u);
        if (v2) {
            v3 = v2->field_4;
            v2->field_0 = 44;
            new (v3) slab_list_t[44];
        }

        slab_full_list = v3;

        auto *v4 = (slab_list_t *) operator new(12u);
        if (v4 != nullptr) {
            v4->_first_element = nullptr;
            v4->_last_element = nullptr;
            v4->_size = 0;
        } else {
            v4 = nullptr;
        }

        constexpr uint32_t EXPANDED_SLAB_COUNT = 16384u; // 64 MB (16384 slabs * 4096 bytes)
        constexpr uint32_t EXPANDED_ARENA_SIZE = 4096u * EXPANDED_SLAB_COUNT;

        slab_free_list = v4;
        static_slab_arena = static_cast<char *>(arch_memalign(4096u, EXPANDED_ARENA_SIZE));
        static_slab_headers = static_cast<slab_t *>(arch_malloc(sizeof(slab_t) * EXPANDED_SLAB_COUNT));
        auto *arena = static_slab_arena;
        auto *headers = static_slab_headers;
        for (auto i = 0u; i < EXPANDED_SLAB_COUNT; ++i)
        {
            new (headers) slab_t {arena};
            headers->set(20);

            slab_free_list->push_back(headers++);

            arena += 4096u;
        }

        initialized = true;

    }
    else
    {
        CDECL_CALL(0x0059F5A0);
    }
}

slab_allocator::slab_t::iterator slab_allocator::slab_list_t::push_back(slab_allocator::slab_t *tmp) {
    assert(tmp != nullptr);
    assert(tmp->simple_list_vars._sl_next_element == nullptr);
    assert(tmp->simple_list_vars._sl_prev_element == nullptr);
    assert(tmp->simple_list_vars._sl_list_owner == nullptr);

    if (this->_last_element != nullptr) {
        assert(_last_element->simple_list_vars._sl_next_element == nullptr);

        this->_last_element->simple_list_vars._sl_next_element = tmp;
        tmp->simple_list_vars._sl_prev_element = this->_last_element;
        tmp->simple_list_vars._sl_next_element = nullptr;
        this->_last_element = tmp;
        tmp->simple_list_vars._sl_list_owner = this;

        slab_t::iterator a2{tmp};
        return a2;
    }

    auto it = this->add_slab(tmp);

    return it;
}

bool slab_allocator::slab_list_t::remove_slab(slab_allocator::slab_t *slab) {
    if (slab == nullptr) {
        return false;
    }

    auto v2 = slab->simple_list_vars._sl_list_owner;
    if (v2 != this) {
        return false;
    }

    auto *v3 = slab->simple_list_vars._sl_prev_element;
    if (v3 != nullptr) {
        v3->simple_list_vars._sl_next_element = slab->simple_list_vars._sl_next_element;
    } else {
        v2->_first_element = slab->simple_list_vars._sl_next_element;
    }

    auto *v4 = slab->simple_list_vars._sl_next_element;
    auto *v5 = slab->simple_list_vars._sl_prev_element;
    if (v4 != nullptr) {
        v4->simple_list_vars._sl_prev_element = v5;
    } else {
        slab->simple_list_vars._sl_list_owner->_last_element = v5;
    }

    --slab->simple_list_vars._sl_list_owner->_size;

    slab->simple_list_vars = {};
    return true;
}

slab_allocator::slab_t::iterator slab_allocator::slab_list_t::add_slab(slab_allocator::slab_t *tmp) {
    assert(tmp != nullptr);
    assert(tmp->simple_list_vars._sl_next_element == nullptr);
    assert(tmp->simple_list_vars._sl_prev_element == nullptr);
    assert(tmp->simple_list_vars._sl_list_owner == nullptr);

    tmp->simple_list_vars._sl_next_element = this->_first_element;
    tmp->simple_list_vars._sl_prev_element = nullptr;
    if (this->_first_element != nullptr) {
        this->_first_element->simple_list_vars._sl_prev_element = tmp;
    }

    this->_first_element = tmp;
    if (tmp->simple_list_vars._sl_next_element == nullptr) {
        this->_last_element = tmp;
    }

    tmp->simple_list_vars._sl_list_owner = this;
    ++this->_size;

    slab_t::iterator a2{tmp};
    return a2;
}

void *slab_allocator::allocate(int size, slab_allocator::slab_t **a2) {
    assert(size >= 0);
    assert(size <= MAX_OBJECT_SIZE);

    if (!initialized) {
        initialize();
    }

    int index = 0;
    if (size >= 4) {
        index = (size + 3) / 4 - 1;
    }

    auto *slab = slab_partial_list[index].front();
    if (slab == nullptr) {
        slab = create_slab(size);
    }

    assert(slab != nullptr);
    assert(slab->is_set(SLAB_ON_PARTIAL_LIST));

    auto *obj = slab->get_obj();
    assert(obj != nullptr);

    if (a2 != nullptr) {
        *a2 = slab;
    }

    ++allocated_object_count[index];
    --free_object_count[index];

    if (!slab->get_free_object_count()) {
        slab->unset(SLAB_ON_PARTIAL_LIST);

        assert(slab_partial_list[index].front() == slab);

        auto *v8 = slab_partial_list[index]._first_element;
        if (v8 != nullptr) {
            auto *v9 = v8->simple_list_vars._sl_prev_element;
            auto *v10 = v8->simple_list_vars._sl_next_element;
            if (v9 != nullptr) {
                v9->simple_list_vars._sl_next_element = v10;
            } else {
                v8->simple_list_vars._sl_list_owner->_first_element = v10;
            }

            auto *v11 = v8->simple_list_vars._sl_next_element;
            auto *v12 = v8->simple_list_vars._sl_prev_element;
            if (v11 != nullptr) {
                v11->simple_list_vars._sl_prev_element = v12;
            } else {
                v8->simple_list_vars._sl_list_owner->_last_element = v12;
            }

            --v8->simple_list_vars._sl_list_owner->_size;

            v8->simple_list_vars = {};
        }

        slab_full_list[index].push_back(slab);

        slab->set(SLAB_ON_FULL_LIST);
        --partial_slab_count[index];
        ++full_slab_count[index];
    }

    return obj;
}

uint32_t slab_allocator::get_max_object_size() {
    return 176;
}

slab_allocator::slab_t::slab_t(char *a2) {
    this->simple_list_vars = {};

    this->begin_sentry = SLAB_HEADER_BEGIN_SENTRY;
    this->arena = a2;
    this->field_8 = 0;
    this->m_size = 0;
    this->total_object_count = 0;
    this->alloc_object_count = 0;
    this->field_10 = nullptr;
    this->end_sentry = SLAB_HEADER_END_SENTRY;
}

bool slab_allocator::slab_t::contains(uint32_t a2) {
    uint32_t v2 = (uint32_t) this->arena;
    return (a2 >= v2 && a2 < v2 + 4096);
}

void slab_allocator::slab_t::sub_592D00(int16_t size) {
    if constexpr (1) {
        assert(this->begin_sentry == SLAB_HEADER_BEGIN_SENTRY);
        assert(this->end_sentry == SLAB_HEADER_END_SENTRY);
        assert(alloc_object_count == 0);
        assert(size > 0);

        this->m_size = size;
        this->total_object_count = 4096 / this->m_size;
        auto *v2 = this->arena;

        this->field_10 = nullptr;
        for (int i = 0; i < this->total_object_count; ++i) {
            *(uint32_t *) v2 = bit_cast<uint32_t>(this->field_10);
            this->field_10 = v2;
            v2 += this->m_size;
        }
    } else {
        THISCALL(0x00592D00, this, size);
    }
}

int slab_allocator::slab_t::get_free_object_count() {
    return this->total_object_count - this->alloc_object_count;
}

int slab_allocator::slab_t::get_alloc_object_count() {
    return this->alloc_object_count;
}

void *slab_allocator::slab_t::get_obj() {
    assert(this->begin_sentry == SLAB_HEADER_BEGIN_SENTRY);
    assert(this->end_sentry == SLAB_HEADER_END_SENTRY);
    if (this->field_10 == nullptr) {
        return nullptr;
    }

    auto v2 = this->field_10;
    this->field_10 = *bit_cast<void **>(v2);
    ++this->alloc_object_count;

    assert(alloc_object_count <= total_object_count);

    return v2;
}

bool slab_allocator::slab_t::is_set(int a2) {
    assert(this->begin_sentry == SLAB_HEADER_BEGIN_SENTRY);
    assert(this->end_sentry == SLAB_HEADER_END_SENTRY);
    return (a2 & this->field_8) != 0;
}

void slab_allocator::slab_t::set(int16_t a2) {
    assert(this->begin_sentry == SLAB_HEADER_BEGIN_SENTRY);
    assert(this->end_sentry == SLAB_HEADER_END_SENTRY);

    this->field_8 |= a2;
}

void slab_allocator::slab_t::unset(uint16_t a2) {
    assert(this->begin_sentry == SLAB_HEADER_BEGIN_SENTRY);
    assert(this->end_sentry == SLAB_HEADER_END_SENTRY);

    this->field_8 &= ~a2;
}

void slab_allocator::slab_t::free_object(void *object) {
    assert(this->begin_sentry == SLAB_HEADER_BEGIN_SENTRY);
    assert(this->end_sentry == SLAB_HEADER_END_SENTRY);
    assert(this->contains((uint32_t) object));
    *(uint32_t *) object = bit_cast<uint32_t>(this->field_10);
    this->field_10 = object;

    --this->alloc_object_count;
    assert(alloc_object_count >= 0);
}

int slab_allocator::slab_t::get_total_object_count() {
    return this->total_object_count;
}

//FIXME
slab_allocator::slab_t *slab_allocator::find_slab_for_object(void *obj)
{
    if constexpr (0)
    {
        if (obj == nullptr || !initialized) {
            return nullptr;
        }

#if 0
        sp_log("find_slab_for_object: obj = 0x%08X, static_slab_arena = 0x%08X",
               obj,
               static_slab_arena);
#endif

        if ((obj < static_slab_arena) || (obj >= static_slab_arena + 0x04000000))
        {
            uint32_t uVar3 = bit_cast<uint32_t>(obj) & 0xfffff000;
            auto *slab = (slab_t *) (uVar3 + SLAB_SIZE);

            MEMORY_BASIC_INFORMATION MemInfo;
            if (VirtualQuery(slab, &MemInfo, sizeof(MemInfo)) != 0) {
                if (MemInfo.State == MEM_COMMIT && ((MemInfo.Protect & PAGE_READWRITE) != 0)) {
                    if (slab->begin_sentry == SLAB_HEADER_BEGIN_SENTRY &&
                        slab->end_sentry == SLAB_HEADER_END_SENTRY &&
                        slab->is_set(SLAB_FROM_AUX | SLAB_FROM_HEAP) &&
                        !slab->is_set(SLAB_FROM_STATIC) && (slab->arena == (char *) uVar3) &&
                        slab->contains((uint32_t) obj)) {
                        return slab;
                    } else {
                        //sp_log("find_slab_for_object: 2");
                    }
                } else {
#if 0
                    sp_log(
                        "find_slab_for_object: lpAddress = 0x%08X, State = 0x%08X, Protect & "
                        "PAGE_READWRITE = 0x%08X",
                        slab,
                        MemInfo.State,
                        MemInfo.Protect & PAGE_READWRITE);

#endif
                }

            } else {
                sp_log("find_slab_for_object: 0");
            }

        }
        else
        {
            auto index = (uint32_t) ((char *) obj - (char *) static_slab_arena) >> 12;

            constexpr auto NUM_STATIC_SLABS = 16384;
            assert(index < NUM_STATIC_SLABS);

            auto *slab = &static_slab_headers[index];

            assert(slab->begin_sentry == SLAB_HEADER_BEGIN_SENTRY);
            assert(slab->end_sentry == SLAB_HEADER_END_SENTRY);
            assert(slab->contains((uint32_t) obj));

            assert(slab->arena == &bit_cast<char *>(static_slab_arena)[SLAB_SIZE * index]);
            assert(slab->is_set(SLAB_FROM_STATIC));

            return slab;
        }

        return nullptr;
    }
    else
    {
        slab_t * (*func)(void *) = CAST(func, 0x00592D50);
        return func(obj);
    }
}

slab_allocator::slab_t *slab_allocator::create_slab(int size)
{
    if constexpr (1)
    {
        assert(initialized);

        auto *slab = slab_free_list->front();
        if (slab == nullptr) {
            goto LABEL_9;
        } else {
            auto *v2 = slab->simple_list_vars._sl_prev_element;
            auto v3 = slab->simple_list_vars._sl_next_element;
            if (v2 != nullptr) {
                v2->simple_list_vars._sl_next_element = v3;
            } else {
                slab->simple_list_vars._sl_list_owner->_first_element = v3;
            }

            auto *v4 = slab->simple_list_vars._sl_next_element;
            auto v5 = slab->simple_list_vars._sl_prev_element;
            if (v4 != nullptr) {
                v4->simple_list_vars._sl_prev_element = v5;
            } else {
                slab->simple_list_vars._sl_list_owner->_last_element = v5;
            }

            --slab->simple_list_vars._sl_list_owner->_size;

            slab->simple_list_vars = {};
        }

        if (slab != nullptr) {
            assert(slab->is_set(SLAB_ON_FREE_LIST));

            slab->unset(SLAB_ON_FREE_LIST);

        } else {
        LABEL_9:
            auto *v6 = static_cast<char *>(arch_memalign(4096u, 4132u));
            auto *mem = (v6 + 4096);

            if (mem != nullptr) {
                slab = new (mem) slab_t{v6};
            }

            slab->set(SLAB_FROM_HEAP);

            ++free_slab_count;
            ++total_slab_count;
        }

        int index = 0;
        if (size >= 4) {
            index = (size + 3) / 4 - 1;
        }

        slab->sub_592D00(4 * index + 4);

        slab->set(SLAB_ON_PARTIAL_LIST);

        slab_allocator::slab_partial_list[index].add_slab(slab);

        --free_slab_count;
        free_object_count[index] += slab->get_total_object_count();
        ++partial_slab_count[index];

        return slab;

    } else {
        return (slab_t *) CDECL_CALL(0x0059DE20, size);
    }
}

void slab_allocator::create_slab_debug_menu(debug_menu *parent)
{
    auto *slabs_menu = create_menu("Slabs", debug_menu::sort_mode_t::undefined);
    parent->add_entry(slabs_menu);

    auto *entry = create_menu_entry(mString {"Total Slabs"});
    entry->set_p_ival(&total_slab_count);
    entry->set_value_initialized(true);
    slabs_menu->add_entry(entry);

    entry = create_menu_entry(mString {"Free Slabs"});
    entry->set_p_ival(&free_slab_count);
    entry->set_value_initialized(true);
    slabs_menu->add_entry(entry);

    auto *menu = create_menu("Full Slabs", debug_menu::sort_mode_t::undefined);
    slabs_menu->add_entry(menu);
    for ( auto i = 0; i < 44; ++i )
    {
        auto *entry = create_menu_entry(mString {0, "%3d byte", 4 * i + 4});
        entry->set_value_initialized(true);
        entry->set_p_ival(&full_slab_count[i]);
        menu->add_entry(entry);
    }

    menu = create_menu("Partial Slabs", debug_menu::sort_mode_t::undefined);
    slabs_menu->add_entry(menu);
    for ( auto j = 0; j < 44; ++j )
    {
        auto *entry = create_menu_entry(mString {0, "%3d byte", 4 * j + 4});
        entry->set_value_initialized(true);
        entry->set_p_ival(&partial_slab_count[j]);
        menu->add_entry(entry);
    }

    menu = create_menu("Allocated Objects", debug_menu::sort_mode_t::undefined);
    slabs_menu->add_entry(menu);
    for ( auto k = 0; k < 44; ++k )
    {
        auto *entry = create_menu_entry(mString {0, "%3d byte", 4 * k + 4});
        entry->set_value_initialized(true);
        entry->set_p_ival(&allocated_object_count[k]);
        menu->add_entry(entry);
    }

    menu = create_menu("Free Objects", debug_menu::sort_mode_t::undefined);
    slabs_menu->add_entry(menu);

    for ( auto m = 0; m < 44; ++m )
    {
        auto *entry = create_menu_entry(mString {0, "%3d byte", 4 * m + 4});
        entry->set_value_initialized(true);
        entry->set_p_ival(&free_object_count[m]);
        menu->add_entry(entry);
    }
}

void slab_allocator::deallocate(void *a1, slab_t *slab)
{
    if constexpr (1)
    {
        assert(initialized);

        if (a1 != nullptr)
        {
            if (slab == nullptr) {
                slab = find_slab_for_object(a1);
            }

            assert(slab != nullptr);

            int index = 0;
            auto size = slab->m_size;
            if (size >= 4) {
                index = (size + 3) / 4 - 1;
            }

            slab->free_object(a1);
            --allocated_object_count[index];
            ++free_object_count[index];

            if (slab->is_set(SLAB_ON_FULL_LIST)) {
                assert(slab->get_free_object_count() == 1);

                slab->unset(SLAB_ON_FULL_LIST);

                slab_full_list[index].remove_slab(slab);

                slab_partial_list[index].add_slab(slab);

                slab->set(SLAB_ON_PARTIAL_LIST);
                --full_slab_count[index];
                ++partial_slab_count[index];
            } else {
                assert(slab->is_set(SLAB_ON_PARTIAL_LIST));

                if (slab->get_alloc_object_count())
                {
                    auto *v11 = slab;
                    auto *v12 = slab->simple_list_vars._sl_next_element;

                    slab_t::iterator iter{v11};

                    slab_t::iterator v13;
                    for (v13._ptr = v12; v13._ptr; v12 = v13._ptr) {
                        if (v12->total_object_count - v12->alloc_object_count >=
                            v11->total_object_count - v11->alloc_object_count) {
                            break;
                        }

                        swap(iter, v13);
                        v11 = iter._ptr;
                        if (iter._ptr == nullptr) {
                            break;
                        }

                        v13._ptr = iter._ptr->simple_list_vars._sl_next_element;
                    }

                }
                else
                {
                    slab->unset(SLAB_ON_PARTIAL_LIST);
                    slab_partial_list[index].remove_slab(slab);

                    free_object_count[index] -= slab->get_total_object_count();
                    --partial_slab_count[index];

                    if (slab->is_set(SLAB_FROM_HEAP)) {
                        --total_slab_count;

                        auto *v10 = slab->arena;
                        slab = {};
                        mem_freealign(v10);
                    } else {
                        ++free_slab_count;

                        assert(slab->is_set(SLAB_FROM_STATIC | SLAB_FROM_AUX));

                        slab_free_list->add_slab(slab);
                        slab->set(SLAB_ON_FREE_LIST);
                    }
                }
            }
        }
    } else {
        CDECL_CALL(0x0059DCA0, a1, slab);
    }
}

void swap(slab_allocator::slab_t::iterator &a, slab_allocator::slab_t::iterator &b) {
    assert(a._ptr->simple_list_vars._sl_list_owner == b._ptr->simple_list_vars._sl_list_owner);

    auto v2 = a._ptr->simple_list_vars._sl_list_owner;
    if (v2->_first_element == a._ptr) {
        v2->_first_element = b._ptr;
    } else {
        auto *v3 = b._ptr->simple_list_vars._sl_list_owner;
        if (v3->_first_element == b._ptr) {
            v3->_first_element = a._ptr;
        }
    }

    auto *v4 = a._ptr->simple_list_vars._sl_list_owner;
    if (v4->_last_element == a._ptr) {
        v4->_last_element = b._ptr;
    } else {
        auto *v5 = b._ptr->simple_list_vars._sl_list_owner;
        if (v5->_last_element == b._ptr) {
            v5->_last_element = a._ptr;
        }
    }

    auto *v6 = a._ptr->simple_list_vars._sl_prev_element;
    a._ptr->simple_list_vars._sl_prev_element = b._ptr->simple_list_vars._sl_prev_element;
    b._ptr->simple_list_vars._sl_prev_element = v6;
    auto *v7 = a._ptr->simple_list_vars._sl_prev_element;
    if (v7 != nullptr) {
        v7->simple_list_vars._sl_next_element = a._ptr;
    }

    auto *v8 = b._ptr->simple_list_vars._sl_prev_element;
    if (v8 != nullptr) {
        v8->simple_list_vars._sl_next_element = b._ptr;
    }

    auto *v9 = a._ptr->simple_list_vars._sl_next_element;
    a._ptr->simple_list_vars._sl_next_element = b._ptr->simple_list_vars._sl_next_element;
    b._ptr->simple_list_vars._sl_next_element = v9;
    auto *v10 = a._ptr->simple_list_vars._sl_next_element;
    if (v10 != nullptr) {
        v10->simple_list_vars._sl_prev_element = a._ptr;
    }

    auto *v11 = b._ptr->simple_list_vars._sl_next_element;
    if (v11 != nullptr) {
        v11->simple_list_vars._sl_prev_element = b._ptr;
    }

    std::swap(a._ptr, b._ptr);
}

void slab_allocator::dump_debug_info()
{
    assert(initialized);

    debug_print_va("Dumping slab info");

    int num_heap_slabs = 0;
    int num_static_slabs = 0;
    int num_aux_slabs = 0;

    for (int i = 0; i < 44; ++i)
    {
        int num_full_slabs = 0;

        debug_print_va("Object Size: %d", 4 * i + 4);

        for ( auto v2 : (*slab_full_list) )
        {
            ++num_full_slabs;
            if (v2->is_set(SLAB_FROM_HEAP)) {
                ++num_heap_slabs;
            } else if (v2->is_set(SLAB_FROM_AUX)) {
                ++num_aux_slabs;
            } else {
                assert(v2->is_set(SLAB_FROM_STATIC));
                ++num_static_slabs;
            }
        }

        debug_print_va("  Full slabs: %d", num_full_slabs);

        int num_partial_slabs = 0;

        for ( auto v7 : (*slab_partial_list) )
        {
            ++num_partial_slabs;
            if (v7->is_set(SLAB_FROM_HEAP)) {
                ++num_heap_slabs;
            } else if (v7->is_set(SLAB_FROM_AUX)) {
                ++num_aux_slabs;
            } else {
                assert(v7->is_set(SLAB_FROM_STATIC));
                ++num_static_slabs;
            }
        }

        debug_print_va("  Partial slabs: %d", num_partial_slabs);
        auto *slab = *slab_partial_list[i].begin();

        if (slab != nullptr)
        {
            int v53 = 0;
            int v52 = 0;
            auto v51 = slab->get_total_object_count();
            int v50 = 0;
            for ( auto *v49 : (*slab_partial_list) )
            {
                float v31 = v49->get_alloc_object_count() * 100.0f;
                float v28 = v31 / (double) v49->get_total_object_count();
                auto v27 = v49->get_free_object_count();
                auto v13 = v49->get_alloc_object_count();
                debug_print_va(
                    "    Slab %d: Allocated_Objects = %d, Free_Objects = %d, Utilization = %02.02f",
                    v53,
                    v13,
                    v27,
                    v28);

                v52 += v49->get_alloc_object_count();
                ++v53;

                if (v49->get_alloc_object_count() > v50) {
                    v50 = v49->get_alloc_object_count();
                }

                if (v49->get_alloc_object_count() < v51) {
                    v51 = v49->get_alloc_object_count();
                }
            }

            int v48 = v52 + num_full_slabs * slab->get_total_object_count();
            int v47 = num_partial_slabs * slab->get_total_object_count() - v52;
            float v32 = v48 * 100.0f;

            float v29 = v32 / (double) ((num_full_slabs + num_partial_slabs) * slab->get_total_object_count());
            float v30 = v52 * 100.0f;

            debug_print_va("  Average Utilization: %02.02f ( Including full blocks: %02.02f )",
                   v30 / (double) (num_partial_slabs * slab->get_total_object_count()),
                   v29);

            debug_print_va("  Total Allocated: %d ( %dbytes )", v48, v48 * slab->m_size);

            debug_print_va("  Total Free:      %d ( %dbytes )", v47, v47 * slab->m_size);
        }
    }

    debug_print_va("Slabs in use: %d", num_aux_slabs + num_heap_slabs + num_static_slabs);
    debug_print_va("  Heap:       %d", num_heap_slabs);
    debug_print_va("  Static:     %d", num_static_slabs);
    debug_print_va("  Aux:        %d", num_aux_slabs);

    int v46 = 0;
    int v45 = 0;

    for ( auto v25 : (*slab_free_list) )
    {
        if (v25->is_set(SLAB_FROM_HEAP)) {
            ++v46;
        } else {
            assert(v25->is_set(SLAB_FROM_STATIC));
            ++v45;
        }
    }

    debug_print_va("Free Slabs: %d", v45 + v46);
    debug_print_va("  Heap:     %d", v46);
    debug_print_va("  Static:   %d", v45);
}

void slab_allocator::process_lists()
{
    TRACE("slab_allocator::process_lists");

    dump_debug_info();
    if (g_dump_slab_info)
    {
        dump_debug_info();

        g_dump_slab_info = false;
    }
}

void slab_allocator_patch()
{
    DWORD old;
    VirtualProtect((void *)0x00592D6E, sizeof(uint32_t), PAGE_EXECUTE_READWRITE, &old);
    *(uint32_t *)0x00592D6E = 0x04000000u; // 64 MB
    VirtualProtect((void *)0x00592D6E, sizeof(uint32_t), old, &old);

    SET_JUMP(0x0059F750, slab_allocator::allocate);

    SET_JUMP(0x0059DCA0, slab_allocator::deallocate);

    SET_JUMP(0x0059F5A0, slab_allocator::initialize);

    SET_JUMP(0x0059DE20, slab_allocator::create_slab);

    SET_JUMP(0x0059AF70, slab_allocator::process_lists);

    //SET_JUMP(0x00592D50, slab_allocator::find_slab_for_object);
}
