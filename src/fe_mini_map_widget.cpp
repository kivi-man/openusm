#include "fe_mini_map_widget.h"

#include "camera.h"
#include "common.h"
#include "custom_math.h"
#include "entity.h"
#include "fe_mini_map_dot.h"
#include "func_wrapper.h"
#include "ngl.h"
#include "ngl_mesh.h"
#include "ngl_scene.h"
#include "oldmath_po.h"
#include "os_developer_options.h"
#include "panel_layer.h"
#include "panelfile.h"
#include "panelquad.h"
#include "region.h"
#include "resource_manager.h"
#include "terrain.h"
#include "trace.h"
#include "utility.h"
#include "vtbl.h"
#include "wds.h"

#include "resource_directory.h"

VALIDATE_SIZE(fe_mini_map_widget, 0x3B8u);

fe_mini_map_widget::fe_mini_map_widget()
{
    if constexpr (0)
    {
    }
    else
    {
        THISCALL(0x006343C0, this);
    }
}

fe_mini_map_widget::~fe_mini_map_widget()
{
    this->m_vtbl = 0x00895A00;

    operator delete(this->field_3A4);
    this->field_3A4 = nullptr;

    for ( auto &mat : this->field_4 )
    {
        mat.m_texture = nullptr;
    }
}

void fe_mini_map_widget::Init()
{
    TRACE("fe_mini_map_widget::Init");

    if constexpr (1)
    {
        assert(mini_map_icons == nullptr);

        this->mini_map_icons = PanelFile::UnmashPanelFile("mini_map_items", static_cast<panel_layer>(7));
        if (this->mini_map_icons != nullptr) {
            this->map_icon_spidey = this->mini_map_icons->GetPQ("map_icon_spidey");
            this->map_icon_others = this->mini_map_icons->GetPQ("map_icon_others");
            this->minimap_ring = this->mini_map_icons->GetPQ("minimap_ring");
        }

        assert(mini_map_frame == nullptr);

        this->mini_map_frame = PanelFile::UnmashPanelFile("mini_map_frame", static_cast<panel_layer>(7));

        if (this->mini_map_frame != nullptr) {
            this->map_frame_black = this->mini_map_frame->GetPQ("map_frame_black");
            this->map_frame_white = this->mini_map_frame->GetPQ("map_frame_white");
            this->map_frame_white_stub = this->mini_map_frame->GetPQ("map_frame_white_stub");
            this->compass_base = this->mini_map_frame->GetPQ("compass_base");
            this->compass_arrow = this->mini_map_frame->GetPQ("compass_arrow");
            this->map_frame_map_placeholder = this->mini_map_frame->GetPQ(
                "map_frame_map_placeholder");
            this->field_3A0 = this->mini_map_frame->GetAnimationPointer(0);
        }
    } else {
        THISCALL(0x006432F0, this);
    }
}

void fe_mini_map_widget::PrepareRegions()
{
    TRACE("fe_mini_map_widget::PrepareRegions");

    for (auto &mat : this->field_4 ) {
        mat.m_texture = nullptr;
    }

    if (g_world_ptr == nullptr || g_world_ptr->the_terrain == nullptr) {
        return;
    }

    auto *hero_ptr = g_world_ptr->get_hero_ptr(0);
    if (hero_ptr == nullptr) {
        return;
    }

    auto abs_pos = hero_ptr->get_abs_position();
    auto *outermost_region = g_world_ptr->the_terrain->find_outermost_region(abs_pos);
    if (outermost_region == nullptr) {
        return;
    }

    region_array v18 {};
    build_region_list_radius(&v18, outermost_region, abs_pos, 500.0f, true);

    int v14 = 0;
    for (int i = 0; i < v18.count; ++i)
    {
        if (v14 >= 12) {
            break;
        }

        auto *reg = v18[i];
        if ( reg != nullptr )
        {
            if ( reg->is_loaded() && !reg->is_interior() )
            {
                auto scene_id = reg->get_scene_id(1);
                auto key = create_resource_key_from_path(scene_id.c_str(), RESOURCE_KEY_TYPE_PACK);
                auto *dir = resource_manager::get_resource_directory(key);
                if ( dir != nullptr )
                {
                    auto v13 = reg->get_scene_id(0);
                    tlFixedString v17 {v13.c_str()};
                    this->field_4[v14].m_texture = bit_cast<nglTexture *>(dir->get_tlresource(v17,
                                              TLRESOURCE_TYPE_TEXTURE));
                    reg->get_region_extents(&this->field_244[v14], &this->field_2D4[v14]);
                    ++v14;
                }
            }
        }
    }
}

void fe_mini_map_widget::RenderMeshes(matrix4x4 *a2, float &a4)
{
    TRACE("fe_mini_map_widget::RenderMeshes");

    if constexpr (0)
    {
        uint32_t v5 = 0;
        for ( int i = 0; i < 12; ++i )
        {
            if ( this->field_4[i].m_texture != nullptr ) {
                ++v5;
            }
        }

        nglMesh *mesh = nullptr;
        if ( v5 != 0 )
        {
            nglCreateMesh(0x40000u, v5, 0, nullptr);
            auto *v8 = this->field_4;
            for (int i = 0; i < 12; ++i)
            {
                if ( this->field_4[i].m_texture != nullptr )
                {
                    nglMaterialBase *v10 = ( v8 != nullptr
                                                ? bit_cast<nglMaterialBase *>(&v8->field_4)
                                                : nullptr );

                    auto *v11 = sub_507920(v10, 4, 1, 0, nullptr, D3DPT_TRIANGLESTRIP, true);
                    auto iter = v11->CreateIterator();
                    iter.BeginStrip(4u);

                    auto *v14 = g_world_ptr->get_hero_or_marky_cam_ptr();

                    vector3d v47[3] {};
                    v47[2] = v14->get_abs_position();
                    v47[1] = this->field_244[i] - v47[2];
                    v47[0] = this->field_2D4[i] - v47[2];

                    iter.Write(v47[1], -1, vector2d {1.0, 1.0});
                    ++iter;

                    iter.Write(v47[1], -1, vector2d {1.0, 0.0});
                    ++iter;

                    iter.Write(v47[0], -1, vector2d {0.0, 1.0});
                    ++iter;

                    iter.Write(v47[0], -1, vector2d {0.0, 0.0});
                    ++iter;

                    auto *v36 = iter.field_4->field_4;
                    if ( (v36->Flags & 0x40000) == 0 ) {
                        v36->field_3C.m_vertexBuffer->lpVtbl->Unlock(v36->field_3C.m_vertexBuffer);
                    }
                }
            }

            mesh = nglCloseMesh();
        }

        auto *v39 = g_world_ptr->get_chase_cam_ptr(0);
        auto &abs_po = v39->get_abs_po();
        auto y_facing = abs_po.get_y_facing();
        auto z_facing = abs_po.get_z_facing();

        auto v68 = -dot(z_facing, YVEC);
        float v41 = 0.0;
        vector3d v74 {z_facing[0], 0.0, z_facing[2]};
        if ( std::abs(v74[0]) > 0.0f && std::abs(v74[2]) > 0.0f ) {
            v41 = (dot(z_facing, v74) + 1.0f) * 0.5f;
        }

        vector3d v40 = v41 * z_facing + v68 * y_facing;
        v40[1] = 0.0f;
        v40.normalize();

        auto v69 = -sub_48A720(v40[0], v40[2]);
        auto CenterY = this->compass_base->GetCenterY();
        auto CenterX = this->compass_base->GetCenterX();
        this->compass_arrow->Rotate(CenterX, CenterY, v69 + 3.1415927, true);

        matrix4x4 v81 {};
        v81.make_rotate(YVEC, v69);
        auto *v55 = g_world_ptr->get_chase_cam_ptr(0);

        auto v56 = dot(YVEC, v55->get_abs_po().get_y_facing());

        auto v70 = v56;
        auto v63 = std::abs(v56);
        float v57 = sub_4ADC40(v63);
        if ( v70 < 0.0f ) {
            v57 = -v57;
        }

        a4 = (3.1415927 / 2.0) - v57;
        if ( a4 >= (3.1415927 / 4.0) )
        {
            if ( a4 > (3.1415927 / 2.0) ) {
                a4 = (3.1415927 / 2.0);
            }
        }
        else
        {
            a4 = 3.1415927 / 4.0;
        }

        float v64 = -a4;

        matrix4x4 a3 {};
        a3.make_rotate(XVEC, v64);
        float MINI_MAP_ZOOM = os_developer_options::instance->get_int(mString {"MINI_MAP_ZOOM"}); 
        if ( MINI_MAP_ZOOM >= 50.0f )
        {
            if ( MINI_MAP_ZOOM > 1000.0f ) {
                MINI_MAP_ZOOM = 1000.0f;
            }
        }
        else
        {
            MINI_MAP_ZOOM = 50.0f;
        }

        matrix4x4 v83 {};
        v83.make_translate(vector3d {0, 0, MINI_MAP_ZOOM});
        auto v60 = v81 * a3;
        matrix4x4 a2 = v60 * v83;
        if ( mesh != nullptr) {
            nglListAddMesh(mesh, *bit_cast<math::MatClass<4, 3> *>(&a2), nullptr, nullptr);
        }

    }
    else
    {
        THISCALL(0x00638C30, this, a2, &a4);
    }
}

struct poi_sort_record_t {
    float field_0;
    fe_mini_map_dot *field_4;
};

void sort__poi_sort_record_t(poi_sort_record_t *a1, poi_sort_record_t *a2, int a3)
{
    TRACE("std::sort<poi_sort_record_t>");

    sp_log("%d %f", a3, a1->field_0);

    CDECL_CALL(0x0064BCA0, a1, a2, a3);

    sp_log("%f", a1->field_0);
}

void fe_mini_map_widget::UpdatePOIs(matrix4x4 *a2,
                                    Float a3,
                                    Float a4,
                                    Float a5,
                                    Float a6,
                                    Float a7)
{
    TRACE("fe_mini_map_widget::UpdatePOIs");

    if constexpr (0)
    {
    }
    else
    {
        THISCALL(0x0063AEC0, this, a2, a3, a4, a5, a6, a7);
    }
}

void fe_mini_map_widget::Draw()
{
    if (this == nullptr || this->map_frame_map_placeholder == nullptr) {
        return;
    }

    auto *v2 = this->field_3A0;
    auto v3 = v2 && v2->field_2D;
    if (this->field_3A8 || v3)
    {
        auto *hero_ptr = g_world_ptr->get_hero_ptr(0);

        region *reg;

        if (hero_ptr == nullptr ||
            (reg = g_world_ptr->the_terrain->find_outermost_region(hero_ptr->get_abs_position()),
             reg == nullptr) ||
            !reg->is_interior())
        {
            if (v3)
            {
                this->map_frame_map_placeholder->Draw();
            }
            else
            {
                this->PrepareRegions();
                nglListBeginScene(static_cast<nglSceneParamType>(0));
                nglSetClearFlags(1u);
                nglCurScene()->field_3BA = true;
                nglCurScene()->m_farz = 10000.0;

                float local_vec0[4], local_vec1[4];

                this->map_frame_map_placeholder->GetPos(local_vec0, local_vec1);
                auto v10 = local_vec0[0];
                auto v13 = local_vec0[3];

                auto v14 = local_vec1[0];
                auto v12 = local_vec1[3];

                nglSetViewport(local_vec0[0], local_vec1[0], local_vec0[3], local_vec1[3]);

                static const vector4d stru_892F80 {0, 0, 0, 1};

                char v9 = 0;
                matrix4x4 v19;
                v19.sub_415740(&v9);
                v19.arr[3] = stru_892F80;

                nglSetWorldToViewMatrix({v19});
                nglSetZTestEnable(false);

                matrix4x4 v20;
                float a4;
                this->RenderMeshes(&v20, a4);
                this->UpdatePOIs(&v20, a4, v10, v13, v14, v12);
                nglListEndScene();

                for (auto i = 0u; i < this->field_364.size(); ++i) {
                    this->field_364[i]->Draw();
                }

                this->compass_arrow->Draw();
            }

            this->map_frame_black->Draw();

            this->map_frame_white->Draw();

            this->map_frame_white_stub->Draw();

            this->compass_base->Draw();
        }
    }
}

void fe_mini_map_widget::Update(Float a2)
{
    THISCALL(0x00641810, this, a2);
}

__attribute__((naked)) void safe_mini_map_draw_asm() {
    __asm__ __volatile__(
        "test ecx, ecx\n\t"
        "jz 1f\n\t"
        "cmp dword ptr [ecx + 0x394], 0\n\t"
        "jz 1f\n\t"
        "cmp dword ptr [ecx + 0x398], 0\n\t"
        "jz 1f\n\t"
        "mov eax, dword ptr [0x0095C770]\n\t"
        "test eax, eax\n\t"
        "jz 1f\n\t"
        "mov eax, dword ptr [eax + 0x230]\n\t"
        "test eax, eax\n\t"
        "jz 1f\n\t"
        "mov eax, dword ptr [ecx + 0x39C]\n\t"
        "test eax, eax\n\t"
        "jz 1f\n\t"
        "mov eax, dword ptr [eax]\n\t"
        "test eax, eax\n\t"
        "jz 1f\n\t"
        "cmp dword ptr [eax + 0xA4], 0\n\t"
        "jz 1f\n\t"
        "sub esp, 0xB8\n\t"
        "push ebx\n\t"
        "push esi\n\t"
        "mov esi, ecx\n\t"
        "push 0x0064199A\n\t"
        "ret\n\t"
        "1:\n\t"
        "ret\n\t"
    );
}

static void safe_prepare_regions_cpp(fe_mini_map_widget *self) {
    if (self != nullptr) {
        self->PrepareRegions();
    }
}

static void (*s_safe_prepare_regions_fn)(fe_mini_map_widget *) = safe_prepare_regions_cpp;

__attribute__((naked)) static void safe_prepare_regions_asm() {
    __asm__ __volatile__(
        "push ecx\n\t"
        "call dword ptr [%0]\n\t"
        "add esp, 4\n\t"
        "ret\n\t"
        :
        : "m"(s_safe_prepare_regions_fn)
    );
}

static bool __cdecl should_process_poi(
    fe_mini_map_dot *dot,
    int current_count,
    float hero_x,
    float hero_y,
    float hero_z)
{
    if ((uintptr_t)dot < 0x10000) {
        return false;
    }

    if (!dot->field_24) {
        dot->field_25 = false;
        return false;
    }

    // Scratch buffer in vanilla USM holds 50 records. Never allow more than 45 to prevent overflow.
    if (current_count >= 45) {
        dot->field_25 = false;
        return false;
    }

    float dx = dot->field_14[0] - hero_x;
    float dy = dot->field_14[1] - hero_y;
    float dz = dot->field_14[2] - hero_z;
    float dist_sq = dx * dx + dy * dy + dz * dz;

    if (!(dist_sq >= 0.0f && dist_sq < 1.0e12f)) {
        dot->field_25 = false;
        return false;
    }

    // Minimap radar radius is 200m (40000.0f).
    // Allow up to 300m (90000.0f), or up to 500m (250000.0f) if fewer than 10 dots collected.
    if (dist_sq > 90000.0f) {
        if (current_count >= 10 || dist_sq > 250000.0f) {
            dot->field_25 = false;
            return false;
        }
    }

    return true;
}

static bool (*s_should_process_poi_fn)(fe_mini_map_dot *, int, float, float, float) = should_process_poi;

__attribute__((naked)) static void safe_poi_check_asm() {
    __asm__ __volatile__(
        "push eax\n\t"
        "push ecx\n\t"
        "push edx\n\t"
        
        "push dword ptr [esp + 0x2C]\n\t" // hero_z
        "push dword ptr [esp + 0x2C]\n\t" // hero_y
        "push dword ptr [esp + 0x2C]\n\t" // hero_x
        "push dword ptr [esi + 0x190]\n\t" // current_count
        "push ebp\n\t"                     // dot
        "call dword ptr [%0]\n\t"
        "add esp, 20\n\t"
        
        "test al, al\n\t"
        "pop edx\n\t"
        "pop ecx\n\t"
        "pop eax\n\t"
        "jz 1f\n\t"
        
        "push 0x0063AFCD\n\t"
        "ret\n\t"
        
        "1:\n\t"
        "push 0x0063B083\n\t"
        "ret\n\t"
        :
        : "m"(s_should_process_poi_fn)
    );
}

static void safe_fe_mini_map_dot_draw(fe_mini_map_dot *self)
{
    if ((uintptr_t)self < 0x10000) {
        return;
    }

    if (self->field_24 && self->field_25)
    {
        if ((uintptr_t)self->field_0 >= 0x10000 && *(uintptr_t *)self->field_0 >= 0x10000) {
            self->field_0->Draw();
        }

        if ((uintptr_t)self->field_8 >= 0x10000) {
            nglListAddQuad(self->field_8);
        }

        if ((uintptr_t)self->field_C >= 0x10000) {
            nglListAddQuad(self->field_C);
        }

        if ((uintptr_t)self->field_10 >= 0x10000) {
            nglListAddQuad(self->field_10);
        }

        if (self->highlight_circle_count_down != 0) {
            if ((uintptr_t)self->field_4 >= 0x10000 && *(uintptr_t *)self->field_4 >= 0x10000) {
                self->field_4->Draw();
            }
        }
    }
}

static void (*s_safe_dot_draw_fn)(fe_mini_map_dot *) = safe_fe_mini_map_dot_draw;

__attribute__((naked)) static void safe_fe_mini_map_dot_draw_asm() {
    __asm__ __volatile__(
        "push ecx\n\t"
        "call dword ptr [%0]\n\t"
        "add esp, 4\n\t"
        "ret\n\t"
        :
        : "m"(s_safe_dot_draw_fn)
    );
}

void fe_mini_map_widget_patch()
{
    // Safe Draw prologue
    SET_JUMP(0x00641990, safe_mini_map_draw_asm);

    // Redirect PrepareRegions to prevent buffer overflow past 12 regions (which overwrites field_364)
    SET_JUMP(0x00619690, safe_prepare_regions_asm);

    // Prevent crash at 0x0063AFC3, prevent scratch buffer overflow (limit 45), and filter distant POIs
    {
        DWORD oldProtect;
        VirtualProtect((void *)0x0063AFC3, 10, PAGE_EXECUTE_READWRITE, &oldProtect);
        SET_JUMP(0x0063AFC3, safe_poi_check_asm);
        *(uint8_t *)0x0063AFC8 = 0x90;
        *(uint8_t *)0x0063AFC9 = 0x90;
        *(uint8_t *)0x0063AFCA = 0x90;
        *(uint8_t *)0x0063AFCB = 0x90;
        *(uint8_t *)0x0063AFCC = 0x90;
        VirtualProtect((void *)0x0063AFC3, 10, oldProtect, &oldProtect);
    }

    // Protect fe_mini_map_dot::Draw from null or corrupted pointers
    SET_JUMP(0x0060C5E0, safe_fe_mini_map_dot_draw_asm);
}





