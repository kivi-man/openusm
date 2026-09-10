#include "proximity_map_stack.h"

#include "common.h"
#include "func_wrapper.h"
#include "utility.h"

#include <cstdlib>
#include <malloc.h>

VALIDATE_SIZE(dynamic_proximity_map_stack, 0x18);

Var<dynamic_proximity_map_stack *[number_of_district_proximity_map_stacks]>
    district_proximity_map_stacks { 0x0095C928 };

dynamic_proximity_map_stack::dynamic_proximity_map_stack() {}

void *dynamic_proximity_map_stack::alloc(int size) {
    auto v2 = ~(this->field_C - 1) & (this->field_C + size - 1);
    auto *v3 = &this->field_14[v2];
    this->field_14 = v3;
    return &v3[-v2];
}

void dynamic_proximity_map_stack::release(void *) {}

void init_proximity_map_stacks() {
    CDECL_CALL(0x0053B860);
}

static constexpr int MAX_EXTRA_PROXIMITY_STACKS = 256;
static dynamic_proximity_map_stack *g_extra_stacks[MAX_EXTRA_PROXIMITY_STACKS] = {};
static uint8_t g_extra_stacks_in_use[MAX_EXTRA_PROXIMITY_STACKS] = {};

dynamic_proximity_map_stack * __cdecl custom_pop_proximity_map_stack() {
    // 1. Önce orijinal 8 slotluk havuzu dene (null olmamak şartıyla)
    uint32_t orig_mask = *(uint32_t *)0x0095C948;
    int orig_max = *(int *)0x00921E40; // 8
    auto **orig_slots = (dynamic_proximity_map_stack **)0x0095C928;

    if (orig_slots != nullptr) {
        for (int i = 0; i < orig_max; ++i) {
            if (!(orig_mask & (1 << i)) && orig_slots[i] != nullptr) {
                *(uint32_t *)0x0095C948 = orig_mask | (1 << i);
                return orig_slots[i];
            }
        }
    }

    // 2. Orijinal havuz dolduysa veya henüz yoksa: Genişletilmiş 256'lık havuzdan ver!
    for (int i = 0; i < MAX_EXTRA_PROXIMITY_STACKS; ++i) {
        if (!g_extra_stacks_in_use[i]) {
            g_extra_stacks_in_use[i] = 1;

            if (g_extra_stacks[i] == nullptr) {
                auto *stk = (dynamic_proximity_map_stack *)malloc(sizeof(dynamic_proximity_map_stack));
                stk->m_vtbl = 0x008881B8; // Orijinal USM.EXE vtable
                stk->field_4 = 4;
                stk->field_8 = 0x4000;    // 16 KB buffer
                stk->field_C = 4;

                // 16 bayt hizalı bellek
                char *buf = (char *)_aligned_malloc(0x4000, 16);
                stk->field_10 = buf;
                stk->field_14 = buf;
                g_extra_stacks[i] = stk;
            } else {
                // Önceden ayrılmış stack'i sıfırla (buffer başına al)
                g_extra_stacks[i]->field_14 = g_extra_stacks[i]->field_10;
            }

            return g_extra_stacks[i];
        }
    }

    // Ek havuz da biterse dinamik yeni bir stack döndür (asla NULL olamaz!)
    auto *fallback = (dynamic_proximity_map_stack *)malloc(sizeof(dynamic_proximity_map_stack));
    fallback->m_vtbl = 0x008881B8;
    fallback->field_4 = 4;
    fallback->field_8 = 0x4000;
    fallback->field_C = 4;
    char *buf = (char *)_aligned_malloc(0x4000, 16);
    fallback->field_10 = buf;
    fallback->field_14 = buf;
    return fallback;
}

void __fastcall custom_push_proximity_map_stack([[maybe_unused]] int dummy_ecx, dynamic_proximity_map_stack *stk) {
    if (!stk) return;

    // 1. Orijinal 8'lik havuzda mı kontrol et
    auto **orig_slots = (dynamic_proximity_map_stack **)0x0095C928;
    int orig_max = *(int *)0x00921E40;
    if (orig_slots != nullptr) {
        for (int i = 0; i < orig_max; ++i) {
            if (orig_slots[i] == stk) {
                uint32_t mask = *(uint32_t *)0x0095C948;
                *(uint32_t *)0x0095C948 = mask & ~(1 << i);
                return;
            }
        }
    }

    // 2. Genişletilmiş havuzdaysa kullanım bayrağını temizle
    for (int i = 0; i < MAX_EXTRA_PROXIMITY_STACKS; ++i) {
        if (g_extra_stacks[i] == stk) {
            g_extra_stacks_in_use[i] = 0;
            return;
        }
    }
}

void proximity_map_stack_patch() {
    sp_log("Installing proximity_map_stack_patch...\n");

    // Global interception: 0x005198A0 is the native pop_proximity_map_stack function
    SET_JUMP(0x005198A0, custom_pop_proximity_map_stack);

    // Call site in region::create_proximity_maps
    REDIRECT(0x00544F8F, custom_pop_proximity_map_stack);

    // Call site in region::destroy_proximity_maps
    REDIRECT(0x0051997A, custom_push_proximity_map_stack);

    sp_log("proximity_map_stack_patch installed successfully!\n");
}

