#include "ngl_support.h"

#include "func_wrapper.h"
#include "ngl.h"
#include "ngl_dx_scene.h"
#include "ngl_scene.h"
#include "nglshader.h"
#include "oldmath_po.h"
#include "trace.h"
#include "vtbl.h"

#include <cmath>


void FastListAddMesh(nglMesh *Mesh,
                     const math::MatClass<4, 3> &LocalToWorld,
                     nglMeshParams *MeshParams,
                     nglParamSet<nglShaderParamSet_Pool> *ShaderParams)
{
    TRACE("FastListAddMesh");

    if (ShaderParams != nullptr)
    {
        if (ShaderParams->IsSetParam<nglTintParam>()) {

            auto *col = bit_cast<color *>(ShaderParams->Get<nglTintParam>()->field_0);
            sp_log("color = %f %f %f %f", col->r, col->g, col->b, col->a);
        }
    }

    if constexpr (1)
    {
        if (Mesh == nullptr) return;
        if (MeshParams == nullptr) return;
        if (ShaderParams == nullptr) return;

        // Soft flag checks (don't assert - district meshes may lack these flags)
        if (!(Mesh->Flags & NGLMESH_PROCESSED) && !(Mesh->Flags & NGLMESH_SCRATCH_MESH)) {
            CDECL_CALL(0x00507690, Mesh, LocalToWorld, MeshParams, ShaderParams);
            return;
        }

        if (MeshParams->Flags & NGLP_SCALE) {
            CDECL_CALL(0x00507690, Mesh, LocalToWorld, MeshParams, ShaderParams);
            return;
        }

        if (MeshParams->Flags & NGLP_FORCE_LOD) {
            CDECL_CALL(0x00507690, Mesh, LocalToWorld, MeshParams, ShaderParams);
            return;
        }

        if (Mesh->NLODs != 0)
        {
            math::VecClass<3, 1> v5 = ( (MeshParams->Flags & 1) != 0
                                            ? Mesh->field_20
                                            : sub_414360(Mesh->field_20, LocalToWorld)
                                        );

            math::VecClass<3, 1> a2a = v5;

            auto v18 = sub_414360(a2a, nglCurScene()->WorldToView);

            auto GetLOD = [](nglMesh *a1, float a2) -> nglMesh *
            {
                for ( int i = a1->NLODs - 1; i >= 0; --i )
                {
                    if ( a2 > a1->LODs[i].field_4 ) {
                        return a1->LODs[i].field_0;
                    }
                }

                return a1;
            };
            Mesh = GetLOD(Mesh, v18[2]);
        }

    LABEL_11:

        auto *v12 = new nglMeshNode {};
        v12->field_88 = Mesh;
        std::memcpy(&v12->field_0, &LocalToWorld, sizeof(LocalToWorld));

        ptr_to_po a2a;
        a2a.m_rel_po = CAST(a2a.m_rel_po, &LocalToWorld);
        a2a.m_abs_po = CAST(a2a.m_abs_po, &nglCurScene()->WorldToScreen);

        v12->field_40 = sub_507130(a2a);

        v12->field_84 = 0;
        v12->field_80 = nullptr;
        v12->field_94 = 1.0;

        v12->field_90 = MeshParams;
        v12->field_8C = *ShaderParams;

        for (auto i = 0u; i < Mesh->NSections; ++i)
        {
            auto *MeshSection = Mesh->Sections[i].Section;
            if (MeshSection == nullptr || MeshSection->Material == nullptr || MeshSection->Material->m_shader == nullptr) {
                continue;
            }

            nglPerfInfo().m_num_verts += MeshSection->NVertices;

            nglMaterialBase *v15 = sub_8EA2E0(&v12->field_8C, MeshSection->Material);

            MeshSection->Material->m_shader->AddNode(v12, MeshSection, v15);
        }

        nglPerfInfo().m_num_polys += Mesh->field_3C;

        if (Mesh->File != nullptr) {
            Mesh->File->field_144 = nglFrame();
        }
    } else {
        CDECL_CALL(0x00507690, Mesh, LocalToWorld, MeshParams, ShaderParams);
    }
}

