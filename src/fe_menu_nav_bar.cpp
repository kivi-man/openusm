#include "fe_menu_nav_bar.h"
#include "ps4_controller.h"
#include "localized_string_table.h"
#include "game.h"

#include "common.h"
#include "func_wrapper.h"
#include "trace.h"
#include "utility.h"

#include <cstring>

VALIDATE_SIZE(menu_nav_bar, 0x2Cu);

menu_nav_bar::menu_nav_bar() : field_4() {
    this->text_box = nullptr;
    this->background_a = nullptr;
    this->field_1C = nullptr;
    this->field_20 = nullptr;
    this->field_24 = nullptr;
}

void menu_nav_bar::Load()
{
    TRACE("menu_nav_bar::Load");
    assert(text_box != nullptr && background_a != nullptr);

    THISCALL(0x00612080, this);
}

void menu_nav_bar::AddButtons(menu_nav_bar::button_type a2,
                              menu_nav_bar::button_type a3,
                              global_text_enum a4) {
    if (ps4_controller::instance().is_ps4_active()) {
        const char *btn_str = "";
        switch (a2.field_0) {
            case 0:
            case 1:
            case 2:  btn_str = "~cross"; break;     // Select / Confirm
            case 15: btn_str = "~triangle"; break;  // Back / Cancel
            case 16: btn_str = "~circle"; break;    // Back / Cancel
            case 3:  btn_str = "~square"; break;
            case 4:  btn_str = "~start"; break;
            default: btn_str = "~cross"; break;
        }

        const char *action_text = (g_game_ptr != nullptr && g_game_ptr->field_7C != nullptr)
                                ? g_game_ptr->field_7C->lookup_localized_string(a4)
                                : nullptr;
        if (action_text != nullptr && std::strlen(action_text) > 0) {
            if (!this->field_4.empty()) {
                this->field_4.append("   ");
            }
            this->field_4.append(btn_str);
            this->field_4.append(" ");
            this->field_4.append(action_text);
            return;
        }
    }

    THISCALL(0x006121C0, this, a2, a3, a4);
}

void menu_nav_bar::Reformat() {
    THISCALL(0x006122B0, this);
}

void menu_nav_bar_patch()
{
    FUNC_ADDRESS(address, &menu_nav_bar::AddButtons);
    SET_JUMP(0x006121C0, address);
}
