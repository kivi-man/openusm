#include "femultilinetext.h"
#include "ps4_controller.h"

#include "common.h"
#include "fetextflashinfo.h"
#include "func_wrapper.h"
#include "game.h"
#include "gamepadinput.h"
#include "localized_string_table.h"
#include "mash_config.h"
#include "multilinestring.h"
#include "trace.h"
#include "utility.h"
#include "variables.h"


VALIDATE_OFFSET(FEMultiLineText, lines, 0x88);
VALIDATE_SIZE(FEMultiLineText, 0xA0u);

FEMultiLineText::FEMultiLineText() : FEText()
{
    this->m_vtbl = 0x0087AE58;
    this->field_68 = color32{0};
    this->field_9C = false;
    this->field_9D = false;
    this->field_9E = false;
    this->line_avail_num = 0;
    this->lines = nullptr;
    this->field_80 = 0;
}

FEMultiLineText::FEMultiLineText(font_index a2,
                                 Float a4,
                                 Float a5,
                                 int a6,
                                 panel_layer a7,
                                 Float a8,
                                 int a9,
                                 int a10,
                                 color32 a11)
{
    THISCALL(0x00629250, this, a2, a4, a5, a6, a7, a8, a9, a10, a11);
}

void FEMultiLineText::Draw() {
    if (this->IsShown()) {
        int start_line = this->field_9C ? this->field_90 : 0;
        int end_line = this->field_9C ? this->field_94 : this->field_80;
        this->Draw(start_line, end_line);
    }
}

void FEMultiLineText::_unmash(mash_info_struct *a1, void *a3)
{
    TRACE("FEMultiLineText::unmash");
    FEText::_unmash(a1, a3);
    this->lines = nullptr;
    this->SetNumLines(this->line_avail_num);
}

int FEMultiLineText::_get_mash_sizeof()
{
#if OPENUSM_XBOX_MASH_FORMAT && !defined(OPENUSM_XBPACK_V10)
    return 0x98;
#else
    return 0xA0;
#endif
}

void FEMultiLineText::Draw(int a2, int a3) {
    if (this->IsShown()) {
        if (a2 < 0) {
            a2 = 0;
        }

        if (a3 > this->field_80) {
            a3 = this->field_80;
        }

        auto v4 = this->field_64;
        auto v17 = this->field_4C;
        if ((v4 & 8) != 0) {
            v17 = this->flash_info->GetColor(this->field_4C);
        }

        v17.set_alpha(v17.get_alpha() * this->field_4);

        if ((v4 & 1) == 0) {
            v17.set_alpha(0xFFu);
            v17.set_blue(0xFFu);
            v17.set_green(0xFFu);
            v17.set_red(0xFFu);
        }

        auto v8 = color32::to_int(v17);

        auto v9 = color32::to_int(this->field_68);
        if (a2 < a3) {
            int i = a2;
            int a3a = a3 - a2;
            do {
                auto *v11 = &this->lines[i];
                if (v11->m_font_index != 6) {
                    if (v11->field_10 != "") {
                        auto v16 = this->field_78;
                        auto v15 = this->field_6C;
                        auto v14 = this->field_40;
                        auto v13 = this->field_3C;
                        auto z_value = this->GetZvalue();
                        v11->Draw(z_value, v8, v9, v13, v14, v15, v16);
                    }
                }
                ++i;
                --a3a;
            } while (a3a);
        }
    }
}

void FEMultiLineText::GetPos(Float &a2, Float &a3) {
    a2 = this->field_34[0];
    a3 = this->field_34[1];
}

void FEMultiLineText::SetButtonColor(color32 a2) {
    this->field_68 = a2;
}

mString FEMultiLineText::ReplaceEndlines(mString a2) {
    for (auto i = a2.find("\n", 0); i > 0; i = a2.find("\n", i + 2)) {
        a2.data()[i] = ' ';
        a2.data()[i + 1] = '\n';
    }

    return a2;
}

void FEMultiLineText::SetButtonScale(Float a2) {
    this->field_6C = a2;
}

void FEMultiLineText::SetTextBox(global_text_enum a2, int a3, Float a4) {
    if (g_game_ptr != nullptr && g_game_ptr->field_7C != nullptr) {
        const char *str = g_game_ptr->field_7C->lookup_localized_string(a2);
        if (str != nullptr) {
            FEMultiLineText::string s;
            mString ms{str};
            std::memcpy(&s, &ms, sizeof(mString));
            this->SetTextBoxNoLocalize(s, a3, a4);
        }
    }
}

char *sub_609580(const char *a1, const char *a2, const char *a3) {
    if constexpr (1) {
        auto *v3 = a1;
        auto v4 = strlen(a1);
        auto v12 = strlen(a2);
        auto *v5 = &a3[strlen(a3) + 1];
        uint32_t v11 = v5 - (a3 + 1);
        auto *result = static_cast<char *>(malloc(v4 * (v5 - a3) + 1));
        auto *v7 = result;
        auto *v13 = result;
        auto *v8 = result;
        if (result != nullptr) {
            result[0] = '\0';
            auto *v9 = strstr(a1, a2);
            if (v9 != nullptr) {
                do {
                    std::memcpy(v8, v3, v9 - v3);
                    auto *v10 = &v8[v9 - v3];

                    std::memcpy(v10, a3, v11);
                    v8 = &v10[v11];
                    v3 = &v9[v12];
                    v10[v11] = 0;
                    v9 = strstr(&v9[v12], a2);
                } while (v9);

                v7 = v13;
            }

            strcat(v8, v3);
            result = (char *) std::realloc(v7, strlen(v7) + 1);
        }

        sp_log("a1 = %s, a2 = %s, a3 = %s -> %s", a1, a2, a3, result);

        return result;

    } else {
        return (char *) CDECL_CALL(0x00609580, a1, a2, a3);
    }
}

void __stdcall hook_sub_60A4A0(mString *a1) {
    if (a1 == nullptr || a1->c_str() == nullptr) {
        return;
    }

    std::string s(a1->c_str());
    if (s.empty()) {
        return;
    }

    bool ps4_active = ps4_controller::instance().is_ps4_active();

    if (ps4_active) {
        auto replace_all = [&](const std::string &from, const std::string &to) {
            size_t pos = 0;
            while ((pos = s.find(from, pos)) != std::string::npos) {
                s.replace(pos, from.length(), to);
                pos += to.length();
            }
        };

        // Convert PC keyboard prompt patterns into DualShock button codes
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

        // Preserve ~button tokens intact for MultiLineString
        a1->update_guts(s.c_str(), -1);
    } else {
        auto replace_token = [&](const char *token, const char *rep) {
            if (rep == nullptr) return;
            size_t pos = 0;
            std::string from(token);
            std::string to(rep);
            while ((pos = s.find(from, pos)) != std::string::npos) {
                s.replace(pos, from.length(), to);
                pos += to.length();
            }
        };

        replace_token("~cross", dword_965C24()[GamepadInput::Cross]);
        replace_token("~triangle", dword_965C24()[GamepadInput::Triangle]);
        replace_token("~square", dword_965C24()[GamepadInput::Square]);
        replace_token("~circle", dword_965C24()[GamepadInput::Circle]);
        replace_token("~updown", "UP & DOWN");
        replace_token("~l2", dword_965C24()[GamepadInput::L2]);
        replace_token("~r2", dword_965C24()[GamepadInput::R2]);
        replace_token("~r3", dword_965C24()[GamepadInput::R3]);
        if (s.find("~both_lr") != std::string::npos) {
            char Dest[256]{};
            sprintf(Dest,
                    "\"%s & %s\"",
                    dword_965C24()[GamepadInput::L2],
                    dword_965C24()[GamepadInput::R2]);
            replace_token("~both_lr", Dest);
        }
        replace_token("~right", dword_965C24()[GamepadInput::Right]);
        replace_token("~select", dword_965C24()[GamepadInput::Select]);
        replace_token("~forward", dword_965C24()[GamepadInput::Forward]);
        replace_token("~left", dword_965C24()[GamepadInput::Left]);
        replace_token("~start", dword_965C24()[GamepadInput::Start]);

        a1->update_guts(s.c_str(), -1);
    }
}

void FEMultiLineText::sub_60A4A0(mString &a1) {
    hook_sub_60A4A0(&a1);
}

int FEMultiLineText::MakeBox(char *a2, int a3, int a4, Float a5, Float a6, bool a7)
{
    TRACE("FEMultiLineText::MakeBox");

    return THISCALL(0x0062EAD0, this, a2, a3, a4, a5, a6, a7);
}

bool FEMultiLineText::CheckIfNotTooLong(int a2)
{
    if ( a2 < this->line_avail_num )
    {
        return true;
    }

    if ( this->field_9E )
    {
        sp_log("MultiLineString is too long (cut off).  Number allocated lines: %d\n", this->line_avail_num);
        auto *v4 = this->lines->field_10.c_str();
        sp_log("Start of text: %s\n", v4);
    }
    else
    {
        sp_log("MultiLineString is too long (not cut off).  Number allocated lines: %d\n", this->line_avail_num);
        auto *v3 = this->lines->field_10.c_str();
        sp_log("Start of text: %s\n", v3);

        assert(0 && "MultiLineString is too long");
    }

    return false;
}

void FEMultiLineText::SetTextBoxNoLocalize(FEMultiLineText::string a2, int a7, Float a8) {
    THISCALL(0x00633AB0, this, a2, a7, a8);
}

void FEMultiLineText::SetTextAlloc(global_text_enum a2) {
    if (g_game_ptr != nullptr && g_game_ptr->field_7C != nullptr) {
        const char *str = g_game_ptr->field_7C->lookup_localized_string(a2);
        if (str != nullptr) {
            this->SetTextAllocNoLocalize(str, -1);
        }
    }
}

void FEMultiLineText::SetText(global_text_enum a2) {
    if (g_game_ptr != nullptr && g_game_ptr->field_7C != nullptr) {
        const char *str = g_game_ptr->field_7C->lookup_localized_string(a2);
        if (str != nullptr) {
            FEMultiLineText::string s;
            mString ms{str};
            std::memcpy(&s, &ms, sizeof(mString));
            this->SetTextNoLocalize(s);
        }
    }
}

void FEMultiLineText::AdjustForJustification()
{
    TRACE("FEMultiLineText::AdjustForJustification");
    THISCALL(0x006182D0, this);
}

void FEMultiLineText::_SetTextNoLocalize(FEMultiLineText::string a1) {
    TRACE("FEMultiLineText::SetTextNoLocalize");

    assert(line_avail_num != 0);

    THISCALL(0x0062E720, this, a1);
}

int FEMultiLineText::SetTextAllocNoLocalize(const char *a2, int a3) {
    sp_log("FEMultiLineText::SetTextAllocNoLocalize:");
    return THISCALL(0x0062E8D0, this, a2, a3);
}

void FEMultiLineText::SetTextBoxAlloc(global_text_enum a1, int a3, Float a4) {
    if (g_game_ptr != nullptr && g_game_ptr->field_7C != nullptr) {
        const char *str = g_game_ptr->field_7C->lookup_localized_string(a1);
        if (str != nullptr) {
            this->SetTextBoxAllocNoLocalize(mString{str}, a3, a4);
        }
    }
}

void FEMultiLineText::SetTextBoxAllocNoLocalize(mString a2, int a6, Float a7) {
    sp_log("FEMultiLineText::SetTextBoxAllocNoLocalize:");

    THISCALL(0x00633C00, this, a2, a6, a7);
}

void FEMultiLineText::SetNumLines(int n) {
    TRACE("FEMultiLineText::SetNumLines", std::to_string(n).c_str());

    assert(n != 0);

    this->line_avail_num = n;
    if (this->lines != nullptr) {
        delete[] this->lines;
        this->lines = nullptr;
    }
    this->field_80 = 0;
    if (n > 0) {
        this->lines = new MultiLineString[n];
    }
}

void FEMultiLineText_patch() {
    SET_JUMP(0x0060A4A0, hook_sub_60A4A0);
}
