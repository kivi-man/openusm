#include "loaded_regions_cache.h"

#include "common.h"
#include "fixed_vector.h"
#include "func_wrapper.h"
#include "trace.h"
#include "vector3d.h"
#include "vector4d.h"
#include "terrain.h"
#include "wds.h"
#include "region.h"
#include "entity.h"

#include <algorithm>
#include <cmath>

namespace loaded_regions_cache {

VALIDATE_SIZE(region, 0x1C);

void __cdecl custom_get_regions_intersecting_sphere(const vector4d *sphere, fixed_vector<::region *, 15> *output_array)
{
    if (output_array == nullptr) {
        return;
    }

    output_array->m_size = 0;

    if (sphere == nullptr || g_world_ptr == nullptr || g_world_ptr->get_the_terrain() == nullptr) {
        return;
    }

    auto *trn = g_world_ptr->get_the_terrain();
    if (trn->regions == nullptr || trn->total_regions <= 0) {
        return;
    }

    float cx = sphere->x;
    float cy = sphere->y;
    float cz = sphere->z;
    float r  = sphere->w;
    if (r < 0.0f) {
        r = 0.0f;
    }

    auto add_region_unique = [&](::region *reg) {
        for (uint32_t j = 0; j < output_array->m_size; ++j) {
            if (output_array->m_data[j] == reg) {
                return;
            }
        }
        // Cap at 8 slots: Beenox entity class only holds 2 fixed + 7 extended = 9 regions total!
        // Returning <= 8 guarantees entity::add_me_to_region (0x004F5371) never overflows.
        if (output_array->m_size < 8) {
            output_array->push_back(reg);
        }
    };

    // Hero Priority: If the query is for or near Spider-Man, find his active region
    // and place it at Slot 0 FIRST so he never loses his ground/region and never enters limbo!
    entity *hero = (g_world_ptr != nullptr) ? g_world_ptr->get_hero_ptr(0) : nullptr;
    if (hero != nullptr) {
        vector3d hpos = hero->get_abs_position();
        float ddx = cx - hpos.x;
        float ddy = cy - hpos.y;
        float ddz = cz - hpos.z;
        if (ddx * ddx + ddy * ddy + ddz * ddz < 400.0f) { // Within 20m of hero
            ::region *hero_reg = nullptr;
            for (int i = 0; i < trn->total_regions; ++i) {
                ::region *reg = trn->regions[i];
                if (reg == nullptr || !reg->is_loaded()) continue;
                if (*(void **)((char *)reg + 0x64) == nullptr) continue;

                vector3d min_ext, max_ext;
                THISCALL(0x0052E770, reg, &min_ext, &max_ext);
                if (hpos.x >= min_ext.x && hpos.x <= max_ext.x &&
                    hpos.y >= min_ext.y && hpos.y <= max_ext.y &&
                    hpos.z >= min_ext.z && hpos.z <= max_ext.z)
                {
                    hero_reg = reg;
                    break;
                }
            }
            if (hero_reg == nullptr) {
                float best_dist_sq = 1e30f;
                for (int i = 0; i < trn->total_regions; ++i) {
                    ::region *reg = trn->regions[i];
                    if (reg == nullptr || !reg->is_loaded()) continue;
                    if (*(void **)((char *)reg + 0x64) == nullptr) continue;

                    vector3d min_ext, max_ext;
                    THISCALL(0x0052E770, reg, &min_ext, &max_ext);
                    float mid_x = (min_ext.x + max_ext.x) * 0.5f;
                    float mid_z = (min_ext.z + max_ext.z) * 0.5f;
                    float dx = hpos.x - mid_x;
                    float dz = hpos.z - mid_z;
                    float dist_sq = dx * dx + dz * dz;
                    if (dist_sq < best_dist_sq) {
                        best_dist_sq = dist_sq;
                        hero_reg = reg;
                    }
                }
            }
            if (hero_reg != nullptr) {
                add_region_unique(hero_reg);
            }
        }
    }

    // Pass 1: 3D bounding box overlap check across all loaded regions
    for (int i = 0; i < trn->total_regions; ++i) {
        ::region *reg = trn->regions[i];
        if (reg == nullptr || !reg->is_loaded()) {
            continue;
        }

        // Offset 0x64 holds the OBB / extents node
        if (*(void **)((char *)reg + 0x64) == nullptr) {
            continue;
        }

        vector3d min_ext, max_ext;
        THISCALL(0x0052E770, reg, &min_ext, &max_ext);

        // AABB overlap test: sphere bounding box vs region bounding box
        if (cx + r >= min_ext.x && cx - r <= max_ext.x &&
            cy + r >= min_ext.y && cy - r <= max_ext.y &&
            cz + r >= min_ext.z && cz - r <= max_ext.z)
        {
            add_region_unique(reg);
        }
    }

    // Pass 2: If no regions matched in 3D, check 2D horizontal overlap (X-Z)
    // Critical when Spider-Man swings high above building rooftops or falls below sea level!
    if (output_array->m_size == 0) {
        for (int i = 0; i < trn->total_regions; ++i) {
            ::region *reg = trn->regions[i];
            if (reg == nullptr || !reg->is_loaded()) {
                continue;
            }

            if (*(void **)((char *)reg + 0x64) == nullptr) {
                continue;
            }

            vector3d min_ext, max_ext;
            THISCALL(0x0052E770, reg, &min_ext, &max_ext);

            if (cx + r >= min_ext.x && cx - r <= max_ext.x &&
                cz + r >= min_ext.z && cz - r <= max_ext.z)
            {
                add_region_unique(reg);
            }
        }
    }

    // Pass 3: Hero safeguard / closest loaded region fallback
    // If output is still empty, and the query is for the hero (or near the hero),
    // attach the closest loaded region horizontally so the hero is NEVER stripped of regions into limbo!
    if (output_array->m_size == 0) {
        bool is_hero_query = false;
        if (hero != nullptr) {
            vector3d hpos = hero->get_abs_position();
            float ddx = cx - hpos.x;
            float ddy = cy - hpos.y;
            float ddz = cz - hpos.z;
            if (ddx * ddx + ddy * ddy + ddz * ddz < 400.0f) { // Within 20m of hero
                is_hero_query = true;
            }
        }

        if (is_hero_query) {
            ::region *best_reg = nullptr;
            float best_dist_sq = 1e30f;

            for (int i = 0; i < trn->total_regions; ++i) {
                ::region *reg = trn->regions[i];
                if (reg == nullptr || !reg->is_loaded()) {
                    continue;
                }

                if (*(void **)((char *)reg + 0x64) == nullptr) {
                    continue;
                }

                vector3d min_ext, max_ext;
                THISCALL(0x0052E770, reg, &min_ext, &max_ext);

                float mid_x = (min_ext.x + max_ext.x) * 0.5f;
                float mid_z = (min_ext.z + max_ext.z) * 0.5f;
                float ddx = cx - mid_x;
                float ddz = cz - mid_z;
                float dist_sq = ddx * ddx + ddz * ddz;
                if (dist_sq < best_dist_sq) {
                    best_dist_sq = dist_sq;
                    best_reg = reg;
                }
            }

            if (best_reg != nullptr) {
                add_region_unique(best_reg);
            }
        }
    }
}

void __cdecl custom_get_regions_intersecting_box(const vector3d *p1,
                                                 const vector3d *p2,
                                                 fixed_vector<::region *, 15> *output_array,
                                                 const vector3d *padding)
{
    if (output_array == nullptr) {
        return;
    }

    output_array->m_size = 0;

    if (p1 == nullptr || p2 == nullptr || g_world_ptr == nullptr || g_world_ptr->get_the_terrain() == nullptr) {
        return;
    }

    auto *trn = g_world_ptr->get_the_terrain();
    if (trn->regions == nullptr || trn->total_regions <= 0) {
        return;
    }

    float pad_x = (padding != nullptr) ? std::abs(padding->x) : 0.0f;
    float pad_y = (padding != nullptr) ? std::abs(padding->y) : 0.0f;
    float pad_z = (padding != nullptr) ? std::abs(padding->z) : 0.0f;

    float q_min_x = std::min(p1->x, p2->x) - pad_x;
    float q_min_y = std::min(p1->y, p2->y) - pad_y;
    float q_min_z = std::min(p1->z, p2->z) - pad_z;

    float q_max_x = std::max(p1->x, p2->x) + pad_x;
    float q_max_y = std::max(p1->y, p2->y) + pad_y;
    float q_max_z = std::max(p1->z, p2->z) + pad_z;

    auto add_region_unique = [&](::region *reg) {
        for (uint32_t j = 0; j < output_array->m_size; ++j) {
            if (output_array->m_data[j] == reg) {
                return;
            }
        }
        if (output_array->m_size < 8) {
            output_array->push_back(reg);
        }
    };

    // Pass 1: 3D AABB overlap check across all loaded regions
    for (int i = 0; i < trn->total_regions; ++i) {
        ::region *reg = trn->regions[i];
        if (reg == nullptr || !reg->is_loaded()) {
            continue;
        }

        if (*(void **)((char *)reg + 0x64) == nullptr) {
            continue;
        }

        vector3d min_ext, max_ext;
        THISCALL(0x0052E770, reg, &min_ext, &max_ext);

        if (q_max_x >= min_ext.x && q_min_x <= max_ext.x &&
            q_max_y >= min_ext.y && q_min_y <= max_ext.y &&
            q_max_z >= min_ext.z && q_min_z <= max_ext.z)
        {
            add_region_unique(reg);
        }
    }

    // Pass 2: 2D horizontal overlap check (X-Z) if 3D matched nothing
    // Critical for swing rays or ground elevation checks originating above rooftops
    if (output_array->m_size == 0) {
        for (int i = 0; i < trn->total_regions; ++i) {
            ::region *reg = trn->regions[i];
            if (reg == nullptr || !reg->is_loaded()) {
                continue;
            }

            if (*(void **)((char *)reg + 0x64) == nullptr) {
                continue;
            }

            vector3d min_ext, max_ext;
            THISCALL(0x0052E770, reg, &min_ext, &max_ext);

            if (q_max_x >= min_ext.x && q_min_x <= max_ext.x &&
                q_max_z >= min_ext.z && q_min_z <= max_ext.z)
            {
                add_region_unique(reg);
            }
        }
    }

    // Pass 3: Fallback to horizontally closest loaded region
    if (output_array->m_size == 0) {
        float mid_qx = (q_min_x + q_max_x) * 0.5f;
        float mid_qz = (q_min_z + q_max_z) * 0.5f;

        ::region *best_reg = nullptr;
        float best_dist_sq = 1e30f;

        for (int i = 0; i < trn->total_regions; ++i) {
            ::region *reg = trn->regions[i];
            if (reg == nullptr || !reg->is_loaded()) {
                continue;
            }

            if (*(void **)((char *)reg + 0x64) == nullptr) {
                continue;
            }

            vector3d min_ext, max_ext;
            THISCALL(0x0052E770, reg, &min_ext, &max_ext);

            float mid_rx = (min_ext.x + max_ext.x) * 0.5f;
            float mid_rz = (min_ext.z + max_ext.z) * 0.5f;
            float dx = mid_qx - mid_rx;
            float dz = mid_qz - mid_rz;
            float dist_sq = dx * dx + dz * dz;
            if (dist_sq < best_dist_sq) {
                best_dist_sq = dist_sq;
                best_reg = reg;
            }
        }

        if (best_reg != nullptr) {
            add_region_unique(best_reg);
        }
    }
}

void get_regions_intersecting_sphere_platform_independent(const vector4d &a1, fixed_vector<::region *, 15> *output_array) {
    custom_get_regions_intersecting_sphere(&a1, output_array);
}

void get_regions_intersecting_sphere(const vector3d &a1,
                                     Float a2,
                                     fixed_vector<::region *, 15> *a3)
{
    TRACE("loaded_regions_cache::get_regions_intersecting_sphere");

    vector4d v5;
    v5[0] = a1[0];
    v5[1] = a1[1];
    v5[2] = a1[2];
    v5[3] = a2;
    custom_get_regions_intersecting_sphere(&v5, a3);
}

} // namespace loaded_regions_cache

void loaded_regions_cache_patch()
{
    SET_JUMP(0x00565BF0, loaded_regions_cache::custom_get_regions_intersecting_sphere);
    SET_JUMP(0x0052E8B0, loaded_regions_cache::custom_get_regions_intersecting_box);
}
