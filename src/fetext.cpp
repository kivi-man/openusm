#include "fetext.h"
#include "ps4_controller.h"
#include "multilinestring.h"
#include "femanager.h"

#include "common.h"
#include "fetextflashinfo.h"
#include "func_wrapper.h"
#include "mash_info_struct.h"
#include "mash_config.h"
#include "game.h"
#include "localized_string_table.h"
#include "ngl.h"
#include "trace.h"
#include "utility.h"
#include "variables.h"
#include "vtbl.h"

#include <string>

VALIDATE_SIZE(FEText, 0x68);

FEText::FEText()
{
    THISCALL(0x00617360, this);
}

FEText::FEText(font_index a2,
               global_text_enum a3,
               Float a4,
               Float a5,
               int a6,
               panel_layer a7,
               Float a8,
               int a9,
               int a10,
               color32 a11) {
    THISCALL(0x00617500, this, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
}

void FEText::_unmash(mash_info_struct *a1, void *a3)
{
    TRACE("FEText::unmash");
    if constexpr (1)
    {
        a1->unmash_class_in_place(this->field_1C, this);
        a1->unmash_class_in_place(this->field_50, this);
    }
    else
    {
        void (__fastcall *func)(void *, void *, mash_info_struct *, void *) = CAST(func, get_vfunc(m_vtbl, 0x4));
        func(this, nullptr, a1, a3);
    }
}

int FEText::_get_mash_sizeof()
{
#if OPENUSM_XBOX_MASH_FORMAT && !defined(OPENUSM_XBPACK_V10)
    return 0x60;
#else
    return 0x68;
#endif
}

void FEText::Draw() {
    if (this->IsShown() && !(this->field_1C == mString{""})) {
        auto color = this->field_4C;
        if ((this->field_64 & 8) != 0 && this->flash_info != nullptr) {
            color = this->flash_info->GetColor(this->field_4C);
        }

        auto alpha = color.get_alpha();
        uint8_t v2 = (uint64_t) ((double) alpha * this->field_4);

        color.set_alpha(v2);
        if ((this->field_64 & 1) == 0) {
            color.set_alpha(255u);
            color.set_blue(255u);
            color.set_green(255u);
            color.set_red(255u);
        }

        auto a4 = this->field_34[1];
        auto a3 = this->field_34[0];

        {
            void (__fastcall *field_108)(FEText *, void *, void *, void *) = CAST(field_108, get_vfunc(this->m_vtbl, 0x108));
            if (field_108 != nullptr) {
                field_108(this, nullptr, &a3, &a4);
            }
        }

        nglFont *font = g_femanager.GetFont(this->field_18);

        auto v10 = this->field_40;
        auto v9 = this->field_3C;

        auto a5 = this->GetZvalue();

        auto v14 = color32::to_int(color);
        const char *raw_str = this->field_1C.c_str();

        std::string s(raw_str ? raw_str : "");
        if (ps4_controller::instance().is_ps4_active() && !s.empty()) {
            auto replace_all = [&](const std::string &from, const std::string &to) {
                size_t pos = 0;
                while ((pos = s.find(from, pos)) != std::string::npos) {
                    s.replace(pos, from.length(), to);
                    pos += to.length();
                }
            };

            replace_all("[SPACE]", "~cross");
            replace_all("[Space]", "~cross");
            replace_all("[ENTER]", "~cross");
            replace_all("[Enter]", "~cross");
            replace_all("[ESC]", "~start");
            replace_all("[Esc]", "~start");
            replace_all("[TAB]", "~select");
            replace_all("[Tab]", "~select");
            replace_all("[LMB]", "~square");
            replace_all("[RMB]", "~triangle");
            replace_all("[Left Mouse Button]", "~square");
            replace_all("[Right Mouse Button]", "~triangle");
            replace_all("[E]", "~circle");
            replace_all("[e]", "~circle");
            replace_all("[Q]", "~l2");
            replace_all("[q]", "~l2");
            replace_all("[Left Shift]", "~r2");
            replace_all("[LEFT SHIFT]", "~r2");
            replace_all("[SHIFT]", "~r2");
            replace_all("[Shift]", "~r2");
            replace_all("[W,A,S,D]", "~stick_left");
            replace_all("[WASD]", "~stick_left");
            replace_all("ENTER:", "~cross");
            replace_all("ESC:", "~triangle");
            replace_all("SPACE:", "~cross");
            replace_all("\"SPACE\"", "~cross");
            replace_all("\"ENTER\"", "~cross");
            replace_all("\"ESC\"", "~start");
            replace_all("\"TAB\"", "~select");
        }

        if (s.find('~') != std::string::npos) {
            MultiLineString mls;
            MultiLineString::string mls_str;
            mString temp_ms{s.c_str()};
            std::memcpy(&mls_str, &temp_ms, sizeof(temp_ms));
            mls.field_4[0] = a3;
            mls.field_4[1] = a4;
            mls.Set(mls_str, this->field_18, v9, v10);
            mls.Draw(a5, v14, v14, v9, v10, v9, v10);
        } else {
            nglListAddString(font, s.c_str(), a3, a4, a5, v14, v9, v10);
        }
    }
}

mString FEText::GetName() {
    return this->field_50;
}

void FEText::Update(Float a2) {
    sp_log("0x%08X", m_vtbl);

    void (__fastcall *func)(FEText *, void *, Float) = CAST(func, get_vfunc(m_vtbl, 0x18));
    func(this, nullptr, a2);
}

void FEText::SetText(global_text_enum a2)
{
    if constexpr (0)
    {
        void (__fastcall *func)(FEText *, void *, global_text_enum) = CAST(func, get_vfunc(m_vtbl, 0x88));
        func(this, nullptr, a2);
    }
    else
    {
        auto *table = g_game_ptr->field_7C;
        mString v3 {table->lookup_localized_string(a2)};
        this->SetTextNoLocalize(*bit_cast<FEText::string *>(&v3));
    }
}

void FEText::SetPos(Float a2, Float a3) {
    void (__fastcall *func)(FEText *, void *, Float, Float) = CAST(func, get_vfunc(m_vtbl, 0x90));
    func(this, nullptr, a2, a3);
}

void FEText::SetNoFlash(color32 a2) {
    this->field_4C = a2;
    this->field_64 = this->field_64 & 0xF7 | 1;
}

void FEText::SetScale(Float a2, Float a3)
{
    if constexpr (1)
    {
        float (__fastcall *func)(FEText *, void *, Float, Float) = CAST(func, get_vfunc(m_vtbl, 0x78));
        func(this, nullptr, a2, a3);
    }
    else
    {
        this->field_3C = a2;
        this->field_40 = a3;
    }
}

void FEText::SetScale(Float a2) {
    this->field_3C = a2;
    this->field_40 = a2;
}

void FEText::SetTextNoLocalize(string a1) {
    TRACE("FEText::SetTextNoLocalize");

    if constexpr (0)
    {
        this->field_C = *(decltype(field_C) *)&a1;
    }
    else
    {
        THISCALL(0x0043C410, this, a1);
    }
}

void FEText::SetX(Float a2) {
    this->field_34[0] = a2;
}

void FEText::SetY(Float a2) {
    this->field_34[1] = a2 - flt_965BDC();
}

bool FEText::GetFlag(int a2) {
    return (this->field_64 & a2) != 0;
}

void FEText::AdjustForJustification(float *a2, float *a3)
{
    if constexpr (1) {
        auto *str = this->field_1C.c_str();
        nglFont *font = g_femanager.GetFont(this->field_18);

        uint32_t v13, v14;
        nglGetStringDimensions(font, (char *) str, &v14, &v13, this->field_3C, this->field_40);
        float v8 = v13;
        float v7 = v14;

        //sp_log("%f %f", v8, v7);

        if (this->GetFlag(32)) {
            *a2 = *a2 - v7;
        } else if (!this->GetFlag(16)) {
            *a2 = *a2 - (v7 * 0.5);
        }

        if (this->GetFlag(128)) {
            *a3 = *a3 - v8;
        } else if (!this->GetFlag(64)) {
            *a3 = *a3 - (v8 * 0.5);
        }
    } else {
        THISCALL(0x00617860, this, a2, a3);
    }
}

void FEText::SetFlash(color32 a2, color32 a3, Float a4) {
    this->field_4C = a2;
    auto *v5 = this->flash_info;
    this->field_64 |= 9u;
    if (v5 != nullptr) {
        v5->field_0 = a3;
        this->flash_info->field_C = a4;
    } else {
        this->flash_info = new FETextFlashInfo{a3, a4};
    }
}

float FEText::GetX() {
    float (__fastcall *func)(void *) = CAST(func, get_vfunc(m_vtbl, 0xD4));

    return func(this);
}

float FEText::GetY() {
    float (__fastcall *func)(void *) = CAST(func, get_vfunc(m_vtbl, 0xD8));

    return func(this);
}

void FEText_patch() {
    FUNC_ADDRESS(draw_addr, &FEText::Draw);
    SET_JUMP(0x00617640, draw_addr);
}
