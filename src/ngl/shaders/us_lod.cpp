#include "us_lod.h"

#include "city_lod.h"
#include "func_wrapper.h"
#include "ngl.h"
#include <ngl_dx_texture.h>
#include "trace.h"
#include "utility.h"

static void (__fastcall *original_bind_material)(void *, void *, nglMaterialBase *) = nullptr;

static void __fastcall Hook_BindMaterial(void *self, void *edx, nglMaterialBase *a1)
{
    if (original_bind_material != nullptr) {
        original_bind_material(self, edx, a1);
    }

    // Bind texture to Direct3D stage 0
    auto *lod_tex = (a1 != nullptr && a1->field_1C != nullptr) ? a1->field_1C : city_lod::top_texture();
    if (lod_tex != nullptr) {
        nglDxSetTexture(0, lod_tex, 2u, 3);
        nglSetTextureStageState(0, D3DTSS_COLOROP, 4u);   // D3DTOP_MODULATE
        nglSetTextureStageState(0, D3DTSS_COLORARG1, 2u); // D3DTA_TEXTURE
        nglSetTextureStageState(0, D3DTSS_COLORARG2, 0u); // D3DTA_DIFFUSE
        nglSetTextureStageState(0, D3DTSS_ALPHAOP, 2u);   // D3DTOP_SELECTARG1
        nglSetTextureStageState(0, D3DTSS_ALPHAARG1, 2u); // D3DTA_TEXTURE
        nglSetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0u);
        nglSetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, 0u);
        nglSetTextureStageState(1u, D3DTSS_COLOROP, 1u);  // D3DTOP_DISABLE
        nglSetTextureStageState(1u, D3DTSS_ALPHAOP, 1u);  // D3DTOP_DISABLE
    }
}

void us_lod_patch()
{
    original_bind_material = (void (__fastcall *)(void *, void *, nglMaterialBase *))*(uint32_t *)0x00870D1C;
    set_vfunc(0x00870D1C, (uint32_t)Hook_BindMaterial);
}
