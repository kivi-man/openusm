#include "wds_render_manager.h"

#include "GL/gl.h"

#include "aeps.h"
#include "beam.h"
#include "bitvector.h"
#include "camera.h"
#include "camera_teleport_update_visitor.h"
#include "city_lod.h"
#include "comic_panels.h"
#include "common.h"
#include "culling_params.h"
#include "cut_scene_player.h"
#include "debug_render.h"
#include "femanager.h"
#include "filespec.h"
#include "func_wrapper.h"
#include "game.h"
#include "geometry_manager.h"
#include "glass_house_manager.h"
#include "hierarchical_entity_proximity_map.h"
#include "igofrontend.h"
#include "line_info.h"
#include "loaded_regions_cache.h"
#include "motion_effect_struct.h"
#include "ngl.h"
#include "ngl_mesh.h"
#include "ngl_support.h"
#include "render_text.h"
#include "occlusion.h"
#include "occlusion_visitor.h"
#include "oldmath_po.h"
#include "oriented_bounding_box_root_node.h"
#include "os_developer_options.h"
#include "physical_interface.h"
#include "proximity_map.h"
#include "region.h"
#include "renderoptimizations.h"
#include "sector2d.h"
#include "shadow.h"
#include "subdivision_node_obb_base.h"
#include "trace.h"
#include "terrain.h"
#include "us_colorvol.h"
#include "us_pcuv_shader.h"
#include "utility.h"
#include "variables.h"
#include "vector2di.h"
#include "vector2d.h"
#include "wds.h"

#include <cassert>
#include <cmath>

VALIDATE_SIZE(wds_render_manager, 156u);

struct traversed_entity {
    entity_base_vhandle m_handle;
    int field_4;
};
static Var<fixed_vector<traversed_entity, 750> *> traversed_entities_last_frame {0x0095C7B4};

wds_render_manager::wds_render_manager() {
    this->field_30.sub_56FCB0();

    this->field_0 = new RenderOptimizations();
    this->field_94 = nullptr;
    this->field_98 = 0;

    this->field_10[0] = 0.30000001f;
    this->field_10[1] = -1.0f;
    this->field_10[2] = 0.2f;
    this->field_10[3] = 0;

    this->field_20[0] = 0.80000001f;
    this->field_20[1] = 0.80000001f;
    this->field_20[2] = 0.85000002f;
    this->field_20[3] = 1.0f;

    this->field_60 = {0, 0, 0, 1};

    this->field_90 = 1.0f;
    this->field_5C = nullptr;
    this->field_8C = 0;
    this->field_84 = 480.0f;
    this->field_88 = 750.0f;
}

void show_terrain_info()
{
    if ( g_world_ptr != nullptr )
    {
        auto *v0 = g_world_ptr->get_hero_ptr(0);
        if ( v0 != nullptr )
        {
            auto *v8 = g_world_ptr->get_hero_ptr(0);
            if ( v8->has_physical_ifc() )
            {
                auto *v3 = g_world_ptr->get_hero_ptr(0);
                auto *v4 = v3->physical_ifc();

                string_hash v17;
                v4->get_parent_terrain_type(&v17);

                vector2d v5{512.0, 32.0};
                vector2di v14 {v5};

                auto *v6 = v17.to_string();
                mString v16 {v6};
                color32 v7{255, 255, 255, 255};
                render_text(v16, v14, v7, 1.0, 1.0);
            }
        }
    }
}

void sub_6A9863()
{
    if ( debug_render_get_bval(SPHERES) ) {
        render_debug_spheres();
    }

    if ( debug_render_get_bval(LINES) ) {
        render_debug_lines();
    }

    if ( debug_render_get_ival(LINE_INFO) ) {
        debug_render_line_info();
    }

    render_debug_lines();
    render_debug_spheres();
}

void wds_render_manager::debug_render()
{
    TRACE("wds_render_manager::debug_render");

    if constexpr (0)
    {
        if (os_developer_options::instance->get_flag(mString{"SHOW_TERRAIN_INFO"}))
        {
            show_terrain_info();
        }

        if ( debug_render_get_ival((debug_render_items_e)20) || os_developer_options::instance->get_flag(mString {"SHOW_GLASS_HOUSE"}))
        {
            //glass_house_manager::show_glass_houses();
        }

        //if ( debug_render_get_ival((debug_render_items_e)21) || SHOW_OBBS || SHOW_DISTRICTS )
        {
            auto *ter= g_world_ptr->get_the_terrain();
            ter->show_obbs();
        }

        render_debug_spheres();

        debug_render_line_info();
    }

    sub_6A9863();
}

void wds_render_manager::render_region_mesh(nglMesh *a2, Float fade)
{
    TRACE("wds_render_manager::render_region_mesh");

    sp_log("fade = %f", fade);

    THISCALL(0x00537390, this, a2, fade);
}

int wds_render_manager::add_far_away_entity(vhandle_type<entity> a2) {
    return THISCALL(0x0052A470, this, a2);
}

void wds_render_manager::init_level(const char *a2)
{
    TRACE("wds_render_manager::init_level", a2);
    this->field_84 = 1000.0f;
    this->field_88 = 1500.0f;
    if constexpr (1) {
        if (this->field_5C == nullptr) {
            tlFixedString a1{"obb_shadow000"};
            this->field_5C = nglGetMesh(a1, true);
        }

        if (this->field_94 == nullptr) {
            filespec v7{mString{a2}};

            this->field_94 = new city_lod{v7.m_name.c_str()};
        }
    } else {
        THISCALL(0x00550930, this, a2);
    }
}

void wds_render_manager::create_colorvol_scene()
{
    THISCALL(0x0053DA50, this);
}

void wds_render_manager::render_lowlods(camera &)
{
    if ( os_developer_options::instance->get_flag(mString{"RENDER_LOWLODS"}) ) {
        this->field_94->render();
    }
}

static constexpr auto g_projected_fov_multiplier = 0.80000001f;

void wds_render_manager::update_occluders(camera &a2)
{
    TRACE("wds_render_manager::update_occluders");

    if constexpr (0) {
        occlusion::empty_quad_database();

        for (auto &i : this->field_30.field_0) {
            auto *reg = i.field_0;

            float v18 = reg->get_ground_level();

            auto &v5 = a2.get_abs_po();

            auto &v6 = a2.get_abs_position();

            occlusion_visitor visitor{v6, v5.get_z_facing(), v18, reg};

            ++subdivision_node_obb_base::visit_key();

            auto a4 = a2.compute_xz_projected_fov() * g_projected_fov_multiplier;

            auto &v11 = a2.get_abs_po();

            auto &v12 = a2.get_abs_po();

            vector3d a2a = a2.get_abs_position() - v12.get_z_facing() * 20.f;

            sector2d v26{a2a, v11.get_z_facing(), a4};
            ++region::visit_key2;
            auto *v16 = reg->field_98;
            if (v16 != nullptr) {
                v16->field_5C->traverse_sector_raster(v26, 100.0f, visitor);
            }
        }
    }
    else
    {
        THISCALL(0x00530500, this, &a2);
    }
}

void update_camera_teleport(camera &cam)
{
    TRACE("update_camera_teleport");

    if constexpr (0)
    {
        auto *v1 = g_cut_scene_player();
        auto v17 = ( v1->is_playing() ? 1.0 : 25.0 );

        static Var<vector3d> last_camera_position {0x00960B48};
        static Var<bool> last_camera_position_valid {0x00960B54};

        auto &abs_pos = cam.get_abs_position();
        if ( !last_camera_position_valid() )
        {
            last_camera_position() = abs_pos;
        }

        ++entity::visit_key;

        auto len2 = (last_camera_position() - abs_pos).length2();
        if ( len2 > v17 )
        {
            fixed_vector<region *, 15> a2 {};
            
            camera_teleport_update_visitor_t visitor {};
            loaded_regions_cache::get_regions_intersecting_sphere(abs_pos, culling_params::entity_traversal_distance, &a2);
            for (auto i = 0u; i < a2.size(); ++i) 
            {
                region *reg = a2.at(i);
                if (reg == nullptr || reg->visibility_map == nullptr) continue;

                reg->visibility_map->traverse_sphere(
                                                abs_pos,
                                                culling_params::entity_traversal_distance,
                                                &visitor);
                auto *bitvector_of_legos_rendered_last_frame = reg->bitvector_of_legos_rendered_last_frame;
                if ( bitvector_of_legos_rendered_last_frame != nullptr ) {
                    bitvector_of_legos_rendered_last_frame->clear();
                }

            }

            if ( traversed_entities_last_frame() != nullptr ) {
                traversed_entities_last_frame()->m_size = 0;
            }
        }

        last_camera_position() = cam.get_abs_position();
        last_camera_position_valid() = true;
    } else {
        CDECL_CALL(0x00530760, &cam);
    }
}

void sub_520E60()
{
    CDECL_CALL(0x00520E60);
}

void update_spidey_interface()
{
    if ( g_world_ptr != nullptr )
    {
        if ( g_world_ptr->get_hero_ptr(0) != nullptr ) {
            g_femanager.IGO->UpdateInScene();
        }
    }
}

#include "debug_menu.h"

void wds_render_manager::render(camera &a2, int a3)
{
    TRACE("wds_render_manager::render");

    THISCALL(0x0054B250, this, &a2, a3);
}

void render_data::sub_56FCB0() {
    THISCALL(0x0056FCB0, this);
}

void wds_render_manager::frame_advance(Float a2) {
    TRACE("wds_render_manager::frame_advance");

    THISCALL(0x0054ADE0, this, a2);
}

void wds_render_manager::render_stencil_shadows(const camera &a2)
{
    TRACE("wds_render_manager::render_stencil_shadows");
    
    THISCALL(0x0053D5E0, this, &a2);
}

void wds_render_manager::build_render_data_regions(render_data &a2, camera &a3)
{
    TRACE("wds_render_manager::build_render_data_regions");

    THISCALL(0x00547000, this, &a2, &a3);
}

void wds_render_manager::build_render_data_ents(render_data &a2, camera &a3, int a4)
{
    TRACE("wds_render_manager::build_render_data_ents");

    THISCALL(0x00547250, this, &a2, &a3, a4);
}

void wds_render_manager::clear_colorvol_scene()
{
    USColorVolShaderSpace::gUSColorVolScene() = nullptr;
}

void wds_render_manager::render_meshes(camera &a2)
{
    TRACE("wds_render_manager::render_meshes");

    THISCALL(0x0053CED0, this, &a2);
}

void wds_render_manager::render_legos(camera &a2)
{
    TRACE("wds_render_manager::render_legos");

    THISCALL(0x0053D270, this, &a2);
}

static int g_region_meshes_occluded_this_frame;
static int g_region_meshes_rendered_this_frame;

void wds_render_manager::sub_53D560(camera &a2)
{
    TRACE("wds_render_manager::sub_53D560");

    if constexpr (1)
    {
        g_region_meshes_occluded_this_frame = 0;
        g_region_meshes_rendered_this_frame = 0;
        if ( debug_render_get_bval(REGION_MESHES) ) {
            this->render_meshes(a2);
        }

        if ( debug_render_get_bval(LEGOS) ) {
            this->render_legos(a2);
        }
    }
    else
    {
        THISCALL(0x0053D560, this, &a2);
    }
}

void wds_render_manager_patch()
{
    {
        FUNC_ADDRESS(address, &wds_render_manager::render_region_mesh);
        REDIRECT(0x0053D234, address);

        // REDIRECT(0x00537465, FastListAddMesh); // native 0x00507690 handles mesh addition cleanly
    }

    // REDIRECT(0x0054B410, debug_render_get_bval);

    {
        FUNC_ADDRESS(address, &wds_render_manager::render);
        REDIRECT(0x0054E52D, address);
    }

    // REDIRECT(0x0054B265, update_camera_teleport);

    {
        FUNC_ADDRESS(address, &wds_render_manager::init_level);
        REDIRECT(0x0055B355, address);
    }

    {
        FUNC_ADDRESS(address, &wds_render_manager::render_stencil_shadows);
        REDIRECT(0x0054E585, address);
    }

    {
        FUNC_ADDRESS(address, &wds_render_manager::build_render_data_regions);
        REDIRECT(0x0054B3FB, address);
    }

    {
        FUNC_ADDRESS(address, &wds_render_manager::build_render_data_ents);
        REDIRECT(0x0054B428, address);
    }

    {
        FUNC_ADDRESS(address, &wds_render_manager::sub_53D560);
        REDIRECT(0x0054B403, address);
    }
}

__attribute__((naked)) static void safe_render_section_insert_asm()
{
    __asm__ __volatile__(
        ".byte 0x8B, 0xB8, 0xC0, 0x2B, 0x00, 0x00\n" // mov edi, [eax + 0x2bc0]
        ".byte 0x81, 0xFF, 0x86, 0x01, 0x00, 0x00\n" // cmp edi, 390 (0x186)
        ".byte 0x7D, 0x09\n"                         // jge skip
        ".byte 0x6B, 0xFF, 0x1C\n"                   // imul edi, edi, 0x1c
        ".byte 0x68, 0x05, 0x79, 0x54, 0x00\n"       // push 0x00547905
        ".byte 0xC3\n"                               // ret (jmp 0x00547905)
        // skip:
        ".byte 0x68, 0x42, 0x79, 0x54, 0x00\n"       // push 0x00547942
        ".byte 0xC3\n"                               // ret (jmp 0x00547942)
    );
}

void render_data_ents_patch() {
    // Expand render_data_ents buffer from 750 (0x1770) to 4096 (0x8000) elements
    // so Spider-Man, Peter Parker, and all distant/dense district entities never get dropped!

    auto patch_u32 = [](uint32_t addr, uint32_t val) {
        DWORD oldProtect;
        VirtualProtect((void *)addr, 4, PAGE_EXECUTE_READWRITE, &oldProtect);
        *(uint32_t *)addr = val;
        VirtualProtect((void *)addr, 4, oldProtect, &oldProtect);
    };

    // 1. Allocation sizes: 0x8004 bytes (4096 * 8 + 4) instead of 0x1774 (750 * 8 + 4)
    patch_u32(0x00547268, 0x8004);
    patch_u32(0x00547292, 0x8004);

    // 2. Zeroing loop count: 0x1000 (4096) instead of 0x2EE (750)
    patch_u32(0x00560906, 0x1000);

    // 3. Reset count offset: +0x8000 instead of +0x1770
    patch_u32(0x0056091D, 0x8000);

    // 4. Flush and render reads count offset: +0x8000 instead of +0x1770
    patch_u32(0x0053D5B7, 0x8000);
    patch_u32(0x0053D5D4, 0x8000);

    // 5. Compare count limit: [ecx + 0x8000], 0x1000 (instead of 0x1770, 0x2EE)
    patch_u32(0x005474B9, 0x8000);
    patch_u32(0x005474BD, 0x1000);

    // 6. Entity gathering addition at 0x005474D5:
    // mov ebx, [edx + 0x8000]; cmp ebx, 4088; jge skip; mov [edx+ebx*8], esi; mov [edx+ebx*8+4], eax; inc [edx+0x8000]
    {
        DWORD oldProtect;
        VirtualProtect((void *)0x005474D5, 27, PAGE_EXECUTE_READWRITE, &oldProtect);
        const uint8_t patch[] = {
            0x8B, 0x9A, 0x00, 0x80, 0x00, 0x00, // mov 0x8000(%edx), %ebx
            0x81, 0xFB, 0xF8, 0x0F, 0x00, 0x00, // cmp $4088, %ebx
            0x7D, 0x0D,                         // jge 0x005474F0 (skip only if buffer exceeds 4088)
            0x89, 0x34, 0xDA,                   // mov %esi, (%edx,%ebx,8)
            0x89, 0x44, 0xDA, 0x04,             // mov %eax, 0x4(%edx,%ebx,8)
            0xFF, 0x82, 0x00, 0x80, 0x00, 0x00  // incl 0x8000(%edx)
        };
        memcpy((void *)0x005474D5, patch, 27);
        VirtualProtect((void *)0x005474D5, 27, oldProtect, &oldProtect);
    }

    // 7. Sort count read: [eax + 0x8000]
    patch_u32(0x0054751B, 0x8000);

    // 8. Sorting loops & comparisons: +0x8000
    patch_u32(0x005475DA, 0x8000);
    patch_u32(0x0054761F, 0x8000);
    patch_u32(0x00547951, 0x8000);

    // 9. Frame-end reset: mov dword ptr [ecx + 0x8000], 0
    patch_u32(0x005479B1, 0x8000);

    // 10. Secondary entity insertion points: +0x8000
    patch_u32(0x005309C0, 0x8000);
    patch_u32(0x005309C6, 0x8000);
    patch_u32(0x00562A3D, 0x8000);

    // 11. Guard against buffer overflow at 0x0054793A (0x2BC0 buffer = 400 entries max)
    // When zooming out or opening map at high draw distances, capping at 390 prevents 0x81AA1978 crash!
    {
        DWORD oldProtect;
        VirtualProtect((void *)0x005478FC, 9, PAGE_EXECUTE_READWRITE, &oldProtect);
        SET_JUMP(0x005478FC, safe_render_section_insert_asm);
        *(uint8_t *)0x00547901 = 0x90;
        *(uint8_t *)0x00547902 = 0x90;
        *(uint8_t *)0x00547903 = 0x90;
        *(uint8_t *)0x00547904 = 0x90;
        VirtualProtect((void *)0x005478FC, 9, oldProtect, &oldProtect);
    }
}




