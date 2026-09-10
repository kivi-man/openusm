#include "pc_joypad_device.h"
#include "ps4_controller.h"
#include "gamepadinput.h"

#include "app.h"
#include "common.h"
#include "femanager.h"
#include "frontendmenusystem.h"
#include "func_wrapper.h"
#include "game.h"
#include "input.h"
#include "inputsettings.h"
#include "pausemenusystem.h"
#include "trace.h"
#include "utility.h"
#include "variables.h"

VALIDATE_SIZE(InputState, 0x20);

VALIDATE_SIZE(pc_joypad_device, 0x9Cu);
VALIDATE_OFFSET(pc_joypad_device, field_70, 0x70);

VALIDATE_SIZE(InputCapabilities, 0x28);

int sub_81D1C0(int a1)
{
    auto result = 0u;
    if (!a1) {
        if (ps4_controller::instance().is_ps4_active()) {
            return 1;
        }
        uint32_t v2 = Input::instance() ? Input::instance()->field_129D0 : 0;
        if (v2) {
            result = 1;
        }
        if (v2 > 1) {
            result |= 2u;
        }
        if (v2 > 2) {
            result |= 4u;
        }
        if (v2 > 3) {
            result |= 8u;
        }
    }
    return result;
}

int InputOpen(int a1, unsigned int a2) {
    if (!a1) {
        if (ps4_controller::instance().is_ps4_active() && a2 == 0) {
            return 1;
        }
        if (Input::instance() && a2 < Input::instance()->field_129D0) {
            return a2 + 1;
        }
    }
    return 0;
}

void InputGetCapabilities(int a1, InputCapabilities *pCapabilities) {
    if (pCapabilities != nullptr) {
        pCapabilities->field_0 = 1;
        pCapabilities->field_20 = 0xFFFF;
        pCapabilities->field_24 = 0xFFFF;
    }
}

pc_joypad_device::pc_joypad_device(int in_port) : input_device()
{
    if constexpr (0)
    {
        auto v3 = (!g_master_clock_is_up());

        this->m_vtbl = CAST(m_vtbl, 0x0088EA80);

        if (v3) {
            timeBeginPeriod(1u);
        }

        this->field_94 = timeGetTime();
        this->field_98 = 0;
        auto v4 = sub_81D1C0(0);

        this->field_4 = in_port + 1000000;
        this->field_90 = in_port;
        auto v5 = v4 & (1 << in_port);
        if (v5) {
            this->field_8C = false;
            this->field_88 = false;
        } else {
            this->field_8C = true;
            this->field_88 = true;
        }

        if (v5) {
            std::memset(&this->m_axis_state, 0, sizeof(this->m_axis_state));
            auto v6 = InputOpen(0, in_port);
            this->field_70 = v6;
            if (v6) {
                InputGetCapabilities(v6, &this->field_48);
            }

            std::memset(&this->field_74, 0, sizeof(this->field_74));
        }

    } else {
        THISCALL(0x005991E0, this, in_port);
    }
}

float pc_joypad_device::_get_axis_state(Axis a2, [[maybe_unused]] int a3)
{
    float result;

    // sp_log("%d %d", int(a2), a3);

    if (a2 == 22) {
        result = (this->field_88 == 1);

    } else {
        InputState input_state = this->m_axis_state;
        result = this->_get_axis_state(a2, input_state);
    }
    return result;
}

float pc_joypad_device::_get_axis_old_state(Axis a2, [[maybe_unused]] int a3)
{
    if (a2 == 22) {
        return this->field_8C;
    }

    auto v5 = this->m_axis_old_state;
    return this->_get_axis_state(a2, v5);
}

float pc_joypad_device::_get_axis_delta(Axis a2, [[maybe_unused]] int a3)
{
    if (a2 == 22) {
        return (this->field_88 - this->field_8C);
    }

    InputState axis_state;
    std::memcpy(&axis_state, &this->m_axis_state, sizeof(axis_state));
    auto v7 = this->_get_axis_state(a2, axis_state);

    auto a2a = v7;
    std::memcpy(&axis_state, &this->m_axis_old_state, sizeof(axis_state));
    return a2a - this->_get_axis_state(a2, axis_state);
}

bool pc_joypad_device::_is_connected() {
    return this->field_88 == 0;
}

void pc_joypad_device::_clear_state()
{
    std::memset(&this->m_axis_state, 0, sizeof(this->m_axis_state));
    std::memset(&this->m_axis_old_state, 0, sizeof(this->m_axis_old_state));
}

#include "ps4_controller.h"

int InputGetState(unsigned int dwUserIndex, InputState &pState)
{
    if constexpr (1)
    {
        Input::instance()->poll();

        pState.field_0 = 0;
        pState.m_flags = 0;
        pState.field_5 = 0;
        pState.m_punch = 0;
        pState.m_kick = 0;
        pState.m_jump = 0;
        pState.m_throw_web = 0;
        pState.m_black_button = 0;
        pState.m_stick_to_walls = 0;
        pState.field_C = 0;
        pState.field_D = 0;
        pState.field_10 = 0;
        pState.field_14 = 0;
        pState.field_18 = 0;
        pState.field_1C = 0;

        InputSettings *settings = nullptr;
        if (dwUserIndex >= 1 && dwUserIndex <= 4) {
            settings = Input::instance()->field_129D8[dwUserIndex - 1];
        } else if (dwUserIndex == 0) {
            settings = Input::instance()->field_129D8[0];
        }

        if (settings != nullptr) {
            auto &v3 = settings->field_18;

            auto func = [&v3](int &result, InputAction minInput, InputAction maxInput) {
                int min = (int)(v3.get_state(minInput) * -32767.0f);
                int max = (int)(v3.get_state(maxInput) * 32767.0f);
                auto abs_min = std::abs(min);
                auto abs_max = std::abs(max);
                if (abs_min <= abs_max) {
                    if (abs_min != abs_max) {
                        result = max;
                    }
                } else {
                    result = min;
                }
            };

            func(pState.field_10, InputAction::TurnLeft, InputAction::TurnRight);
            func(pState.field_14, InputAction::Backward, InputAction::Forward);
            func(pState.field_18, InputAction::CameraLeft, InputAction::CameraRight);
            func(pState.field_1C, InputAction::CameraDown, InputAction::CameraUp);

            static constexpr float flt_871978 = 255.f;

            pState.m_jump = (uint8_t)(v3.get_state(InputAction::Jump) * flt_871978);
            pState.m_stick_to_walls = (uint8_t)(v3.get_state(InputAction::StickToWalls) * flt_871978);
            pState.m_punch = (uint8_t)(v3.get_state(InputAction::Punch) * flt_871978);
            pState.m_kick = (uint8_t)(v3.get_state(InputAction::Kick) * flt_871978);
            pState.m_black_button = (uint8_t)(v3.get_state(InputAction::BlackButton) * flt_871978);
            pState.m_throw_web = (uint8_t)(v3.get_state(InputAction::ThrowWeb) * flt_871978);
            pState.field_C = (uint8_t)(v3.get_state(static_cast<InputAction>(10u)) * flt_871978);
            pState.field_D = (uint8_t)(v3.get_state(static_cast<InputAction>(11u)) * flt_871978);

            if (0.0f != v3.get_state(InputAction::Pause)) {
                pState.m_flags |= 0x10u;
            }
            if (0.0f != v3.get_state(InputAction::BackButton)) {
                pState.m_flags |= 0x20u;
            }
            if (0.0f != v3.get_state(static_cast<InputAction>(14u))) {
                pState.m_flags |= 0x40u;
            }
            if (0.0f != v3.get_state(InputAction::CameraCenter)) {
                pState.m_flags |= 0x80u;
            }
            if (0.0f != v3.get_state(static_cast<InputAction>(24u))) {
                pState.m_flags |= 1u;
            }
            if (0.0f != v3.get_state(static_cast<InputAction>(25u))) {
                pState.m_flags |= 2u;
            }
            if (0.0f != v3.get_state(static_cast<InputAction>(26u))) {
                pState.m_flags |= 4u;
            }
            if (0.0f != v3.get_state(static_cast<InputAction>(27u))) {
                pState.m_flags |= 8u;
            }
        }

        // Merge Gamepad controller input (PS4 / DirectInput / XInput)
        unsigned int pad_idx = (dwUserIndex > 0) ? (dwUserIndex - 1) : 0;
        ps4_controller::instance().poll(pad_idx, pState);

        return 0;
    }
    else
    {
        return CDECL_CALL(0x0081D240, dwUserIndex, pState);
    }
}

void pc_joypad_device::_poll()
{
    ps4_controller::instance().update();

    if (ps4_controller::instance().is_ps4_active()) {
        static bool s_configured = false;
        if (!s_configured) {
            setup_gamepad_default_bindings();
            s_configured = true;
        }

        static bool s_tokens_set = false;
        if (!s_tokens_set) {
            dword_965C24()[GamepadInput::Cross]    = const_cast<char*>("~cross");
            dword_965C24()[GamepadInput::Circle]   = const_cast<char*>("~circle");
            dword_965C24()[GamepadInput::Square]   = const_cast<char*>("~square");
            dword_965C24()[GamepadInput::Triangle] = const_cast<char*>("~triangle");
            dword_965C24()[GamepadInput::L2]       = const_cast<char*>("~l2");
            dword_965C24()[GamepadInput::R2]       = const_cast<char*>("~r2");
            dword_965C24()[GamepadInput::Start]    = const_cast<char*>("~start");
            dword_965C24()[GamepadInput::Select]   = const_cast<char*>("~select");
            dword_965C24()[GamepadInput::R3]       = const_cast<char*>("~click_right_stick");
            dword_965C24()[GamepadInput::Forward]  = const_cast<char*>("~stick_left");
            dword_965C24()[GamepadInput::Left]     = const_cast<char*>("~stick_left");
            dword_965C24()[GamepadInput::Right]    = const_cast<char*>("~stick_left");
            s_tokens_set = true;
        }
    }

    if constexpr (1)
    {
        static Var<bool> dword_967CE4{0x00967CE4};

        if (this->field_70 == 1)
        {
            if ((g_femanager.m_pause_menu_system != nullptr && g_femanager.m_pause_menu_system->m_index != -1) ||
                (g_femanager.m_fe_menu_system != nullptr && g_femanager.m_fe_menu_system->m_index != -1) ||
                dword_967CE4())
            {
                if (((g_femanager.m_pause_menu_system != nullptr && g_femanager.m_pause_menu_system->m_index != -1) ||
                     (g_femanager.m_fe_menu_system != nullptr && g_femanager.m_fe_menu_system->m_index != -1)) &&
                    dword_967CE4())
                {
                    dword_967CE4() = false;
                    Input::instance()->sub_8203F0(0, g_inputSettingsMenu());
                }
            }
            else
            {
                dword_967CE4() = true;
                Input::instance()->sub_8203F0(0, g_inputSettingsInGame());
            }
        }

        this->field_88 = 0;

        if (this->field_70)
        {
            if (this->field_8C) {
                this->_clear_state();
            }

            this->m_axis_old_state = this->m_axis_state;
            auto v2 = InputGetState(this->field_70, this->m_axis_state);
            if (v2 != 0)
            {
                this->field_98 = 0;
                return;
            }

            Var<BOOL> dword_9363E4{0x009363E4};

            if (g_game_ptr != nullptr) {
                if (dword_9363E4()) {
                    if (this->m_axis_state.m_black_button > 50u) {
                        dword_9363E4() = false;
                    }
                }

                if (!dword_9363E4() && this->m_axis_state.m_black_button == 0) {
                    dword_9363E4() = true;
                }
            }
        }
    } else {
        THISCALL(0x0058E5C0, this);
    }
}

double sub_58E7F0(int a1) {
    float result = 0.0f;
    float v3 = a1;
    auto a1a = v3;
    if (v3 > 7000.0f || a1a < -7000.0f) {
        if (a1 <= 0) {
            result = -(((double) -a1 - 7000.0f) * 0.000038809329f);
        } else {
            result = (a1a - 7000.0f) * 0.000038809329f;
        }
    }

    if (std::abs(result) < 0.1f) {
        result = 0.0f;
    }

    return result;
}

float pc_joypad_device::_get_axis_state(Axis axis, InputState input_state)
{
    TRACE("pc_joypad_device::get_axis_state");

    auto result = 0.0f;

    float v5;
    double a2a;
    switch (axis) {
    case 0: {
        if ((input_state.m_flags & 4) != 0) {
            return (-1.0f);
        }

        return ((input_state.m_flags & 8) != 0);
    }
    case 1: {
        if ((input_state.m_flags & 1) != 0) {
            return (-1.0f);
        }

        return ((input_state.m_flags & 2) != 0);
    }
    case 2: {
        a2a = sub_58E7F0(input_state.field_10);
        if ((input_state.m_flags & 8) != 0)
        {
            result = 1.0f;
            v5 = (-1.0f);

            if (a2a == v5) {
                return 0.0f;
            }

            return result;
        }

        if ((input_state.m_flags & 4) == 0) {
            return a2a;
        }

        result = (-1.0f);
        v5 = 1.0f;
        if (a2a == v5) {
            return 0.0f;
        }

        return result;
    }
    case 3: {
        a2a = -sub_58E7F0(input_state.field_14);
        if ((input_state.m_flags & 1) != 0) {
            result = (-1.0f);
            v5 = 1.0f;
        } else {
            if ((input_state.m_flags & 2) == 0) {
                return a2a;
            }
        }

        if (a2a == v5) {
            return 0.0f;
        }

        return result;
    }
    case 4:
        return sub_58E7F0(input_state.field_10);
    case 5:
        return -sub_58E7F0(input_state.field_14);
    case 6: {
        auto result = ((input_state.m_flags & 0x40) != 0);
        return result;
    }
    case 7:
        return sub_58E7F0(input_state.field_18);
    case 8: {
        result = -sub_58E7F0(input_state.field_1C);
        return result;
    }
    case 9: { // CameraCenter
        auto result = ((input_state.m_flags & 0x80u) != 0);

        return result;
    }
    case Axis::Jump: {
        return (input_state.m_jump > 30u ? 1.0f : 0.0f);
    }
    case 11: {
        return (input_state.m_stick_to_walls > 30u ? 1.0f : 0.0f);
    }
    case 12:
    case 15: {
        return 0.0f;
    }
    case 13: {
        return (input_state.m_punch > 30u ? 1.0f : 0.0f);
    }
    case 14: {
        return (input_state.m_kick > 30u ? 1.0f : 0.0f);
    }
    case 16: {
        return (input_state.field_C > 30u ? 1.0f : 0.0f);
    }
    case 17: {
        return (input_state.field_D > 30u ? 1.0f : 0.0f);
    }
    case 18: {
        return (input_state.m_black_button > 30u ? 1.0f : 0.0f);
    }
    case 19: {
        return (input_state.m_throw_web > 30u ? 1.0f : 0.0f);
    }
    case 20: {
        return ((input_state.m_flags & 0x10) != 0);
    }
    case 21: {
        return ((input_state.m_flags & 0x20) != 0);
    }
    case 22: {
        return (this->field_88 != 0);
    }
    default:
        assert(0 && "Invalid axis.");

        return result;
    }
}

int __cdecl InputGetState_Hook(unsigned int dwUserIndex, InputState *pState)
{
    if (pState == nullptr) {
        return 1;
    }

    return InputGetState(dwUserIndex, *pState);
}

void pc_joypad_device_patch()
{
    SET_JUMP(0x0081D1C0, sub_81D1C0);
    SET_JUMP(0x0081D0F0, InputOpen);
    SET_JUMP(0x0081D180, InputGetCapabilities);
    SET_JUMP(0x0081D240, InputGetState_Hook);
}



