#include "ps4_controller.h"
#include "pc_joypad_device.h"
#include "rumble_struct.h"
#include "rumble_manager.h"
#include "input_mgr.h"
#include "ai_std_hero.h"
#include "ai_player_controller.h"
#include "game.h"
#include "game_settings.h"
#include "femanager.h"
#include "igofrontend.h"
#include "fe_health_widget.h"
#include "wds.h"
#include "actor.h"
#include "damage_interface.h"
#include "input.h"
#include "utility.h"

#include <windows.h>
#include <setupapi.h>
#include <dinput.h>
#include <xinput.h>
#include <cmath>
#include <cstring>
#include <algorithm>

typedef DWORD (WINAPI *XInputGetState_t)(DWORD, XINPUT_STATE*);
typedef DWORD (WINAPI *XInputSetState_t)(DWORD, XINPUT_VIBRATION*);
typedef BOOLEAN (WINAPI *HidD_SetOutputReport_t)(HANDLE, PVOID, ULONG);
typedef void (WINAPI *HidD_GetHidGuid_t)(LPGUID);

static XInputGetState_t pXInputGetState = nullptr;
static XInputSetState_t pXInputSetState = nullptr;
static HidD_SetOutputReport_t pHidD_SetOutputReport = nullptr;
static HidD_GetHidGuid_t pHidD_GetHidGuid = nullptr;
static HMODULE hXInput = nullptr;
static HMODULE hHid = nullptr;

static uint32_t ds4_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc = crc >> 1;
            }
        }
    }
    return ~crc;
}

ps4_controller& ps4_controller::instance() {
    static ps4_controller s_instance;
    return s_instance;
}

ps4_controller::ps4_controller()
    : m_hid_handle(INVALID_HANDLE_VALUE),
      m_is_bluetooth(false),
      m_connected(false),
      m_last_hid_search_tick(0),
      m_led_r(0),
      m_led_g(180),
      m_led_b(255),
      m_rumble_left(0),
      m_rumble_right(0),
      m_rumble_timer(0.0f),
      m_last_sent_r(0),
      m_last_sent_g(0),
      m_last_sent_b(0),
      m_last_sent_rl(0),
      m_last_sent_rr(0),
      m_last_send_tick(0),
      m_health_pulse_timer(0.0f),
      m_cur_r(0.0f),
      m_cur_g(180.0f),
      m_cur_b(255.0f),
      m_last_hp(100.0f),
      m_feed_blend(0.0f),
      m_feeding_timer(0.0f)
{
    init();
}

ps4_controller::~ps4_controller() {
    if (m_hid_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hid_handle);
        m_hid_handle = INVALID_HANDLE_VALUE;
    }
    if (hXInput) {
        FreeLibrary(hXInput);
        hXInput = nullptr;
    }
    if (hHid) {
        FreeLibrary(hHid);
        hHid = nullptr;
    }
}

#include <cstdarg>
#include <cstdio>

static void ds4_log(const char* fmt, ...) {
    FILE* f = fopen("ds4_debug.log", "a");
    if (!f) {
        f = fopen("D:\\Ultimate Spider-Man\\ds4_debug.log", "a");
    }
    if (!f) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fclose(f);
}

void ps4_controller::find_and_open_ds4_hid() {
    if (m_hid_handle != INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD now = GetTickCount();
    if (m_last_hid_search_tick != 0 && (now - m_last_hid_search_tick < 1000)) {
        return; // Retry search every 1 second
    }
    m_last_hid_search_tick = now;

    if (!hHid) {
        hHid = LoadLibraryA("hid.dll");
    }
    if (!hHid) return;

    if (!pHidD_GetHidGuid) {
        pHidD_GetHidGuid = (HidD_GetHidGuid_t)(void*)GetProcAddress(hHid, "HidD_GetHidGuid");
    }
    if (!pHidD_SetOutputReport) {
        pHidD_SetOutputReport = (HidD_SetOutputReport_t)(void*)GetProcAddress(hHid, "HidD_SetOutputReport");
    }
    if (!pHidD_GetHidGuid) return;

    GUID hidGuid;
    pHidD_GetHidGuid(&hidGuid);

    HDEVINFO devInfo = SetupDiGetClassDevsA(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return;

    SP_DEVICE_INTERFACE_DATA devInterfaceData;
    devInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    ds4_log("find_and_open_ds4_hid: Starting device scan...\n");

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, NULL, &hidGuid, i, &devInterfaceData); ++i) {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailA(devInfo, &devInterfaceData, NULL, 0, &requiredSize, NULL);
        if (requiredSize == 0) continue;

        PSP_DEVICE_INTERFACE_DETAIL_DATA_A devDetail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)malloc(requiredSize);
        if (!devDetail) continue;
        // CRITICAL FIX: PSP_ is a pointer typedef — on 32-bit sizeof(PSP_) = 4 which is WRONG.
        // Must use sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A) = 5 (DWORD + 1 char).
        devDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

        if (SetupDiGetDeviceInterfaceDetailA(devInfo, &devInterfaceData, devDetail, requiredSize, NULL, NULL)) {
            char path_upper[512];
            strncpy(path_upper, devDetail->DevicePath, 511);
            path_upper[511] = 0;
            for (int c = 0; path_upper[c]; c++) path_upper[c] = (char)toupper((unsigned char)path_upper[c]);

            ds4_log("  Device[%lu]: %s\n", i, devDetail->DevicePath);

            if (strstr(path_upper, "054C") != NULL || strstr(path_upper, "09CC") != NULL ||
                strstr(path_upper, "05C4") != NULL || strstr(path_upper, "0CE6") != NULL ||
                strstr(path_upper, "0DF2") != NULL) {

                ds4_log("  -> Sony device matched! Trying to open...\n");

                HANDLE hDevice = CreateFileA(
                    devDetail->DevicePath,
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    0,
                    NULL
                );

                if (hDevice == INVALID_HANDLE_VALUE) {
                    ds4_log("  -> RW open failed (err %lu), trying WRITE only\n", GetLastError());
                    hDevice = CreateFileA(
                        devDetail->DevicePath,
                        GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL,
                        OPEN_EXISTING,
                        0,
                        NULL
                    );
                }

                if (hDevice == INVALID_HANDLE_VALUE) {
                    ds4_log("  -> WRITE open failed (err %lu), trying 0 access\n", GetLastError());
                    hDevice = CreateFileA(
                        devDetail->DevicePath,
                        0,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL,
                        OPEN_EXISTING,
                        0,
                        NULL
                    );
                }

                if (hDevice != INVALID_HANDLE_VALUE) {
                    ds4_log("  -> Opened handle! Testing USB report...\n");
                    // Test USB 32-byte report 0x05
                    uint8_t test_usb[32] = {0};
                    test_usb[0] = 0x05;
                    test_usb[1] = 0xFF;
                    test_usb[2] = 0x04;
                    test_usb[6] = m_led_r;
                    test_usb[7] = m_led_g;
                    test_usb[8] = m_led_b;

                    BOOL ok = FALSE;
                    DWORD wr = 0;
                    ok = WriteFile(hDevice, test_usb, sizeof(test_usb), &wr, NULL);
                    ds4_log("  -> WriteFile USB: ok=%d err=%lu written=%lu\n", ok, GetLastError(), wr);
                    if (!ok && pHidD_SetOutputReport) {
                        ok = pHidD_SetOutputReport(hDevice, test_usb, sizeof(test_usb));
                        ds4_log("  -> SetOutputReport USB fallback: ok=%d err=%lu\n", ok, GetLastError());
                    }

                    if (ok) {
                        m_hid_handle = hDevice;
                        m_connected = true;
                        m_is_bluetooth = false;
                        ds4_log("  -> SUCCESS: Connected USB DS4!\n");
                        free(devDetail);
                        break;
                    }

                    // Test Bluetooth 78-byte report 0x11 with CRC32
                    ds4_log("  -> USB failed, testing BT report...\n");
                    uint8_t test_bt[78] = {0};
                    test_bt[0] = 0x11;
                    test_bt[1] = 0xC0 | 0x04;
                    test_bt[3] = 0x07;
                    test_bt[8] = m_led_r;
                    test_bt[9] = m_led_g;
                    test_bt[10] = m_led_b;

                    uint8_t crc_data[75];
                    crc_data[0] = 0xA2;
                    std::memcpy(&crc_data[1], test_bt, 74);
                    uint32_t crc = ds4_crc32(crc_data, 75);
                    test_bt[74] = (uint8_t)(crc & 0xFF);
                    test_bt[75] = (uint8_t)((crc >> 8) & 0xFF);
                    test_bt[76] = (uint8_t)((crc >> 16) & 0xFF);
                    test_bt[77] = (uint8_t)((crc >> 24) & 0xFF);

                    ok = FALSE;
                    wr = 0;
                    ok = WriteFile(hDevice, test_bt, sizeof(test_bt), &wr, NULL);
                    ds4_log("  -> WriteFile BT: ok=%d err=%lu written=%lu\n", ok, GetLastError(), wr);
                    if (!ok && pHidD_SetOutputReport) {
                        ok = pHidD_SetOutputReport(hDevice, test_bt, sizeof(test_bt));
                        ds4_log("  -> SetOutputReport BT fallback: ok=%d err=%lu\n", ok, GetLastError());
                    }

                    if (ok) {
                        m_hid_handle = hDevice;
                        m_connected = true;
                        m_is_bluetooth = true;
                        ds4_log("  -> SUCCESS: Connected BT DS4!\n");
                        free(devDetail);
                        break;
                    }

                    ds4_log("  -> Both reports failed, closing handle\n");
                    CloseHandle(hDevice);
                } else {
                    ds4_log("  -> All open attempts failed (last err %lu)\n", GetLastError());
                }
            }
        } else {
            ds4_log("  Device[%lu]: SetupDiGetDeviceInterfaceDetailA failed (err %lu)\n", i, GetLastError());
        }
        free(devDetail);
    }
    SetupDiDestroyDeviceInfoList(devInfo);
}

void ps4_controller::send_hid_output_report() {
    if (!hHid) {
        hHid = LoadLibraryA("hid.dll");
        if (hHid) {
            pHidD_GetHidGuid = (HidD_GetHidGuid_t)(void*)GetProcAddress(hHid, "HidD_GetHidGuid");
            pHidD_SetOutputReport = (HidD_SetOutputReport_t)(void*)GetProcAddress(hHid, "HidD_SetOutputReport");
        }
    }

    if (m_hid_handle == INVALID_HANDLE_VALUE) {
        find_and_open_ds4_hid();
        if (m_hid_handle == INVALID_HANDLE_VALUE) return;
    }

    DWORD now = GetTickCount();
    bool changed = (m_led_r != m_last_sent_r || m_led_g != m_last_sent_g || m_led_b != m_last_sent_b ||
                    m_rumble_left != m_last_sent_rl || m_rumble_right != m_last_sent_rr);
    if (!changed && (now - m_last_send_tick < 30)) {
        return;
    }

    m_last_sent_r = m_led_r;
    m_last_sent_g = m_led_g;
    m_last_sent_b = m_led_b;
    m_last_sent_rl = m_rumble_left;
    m_last_sent_rr = m_rumble_right;
    m_last_send_tick = now;

    BOOL ok = FALSE;
    if (!m_is_bluetooth) {
        uint8_t buf[32] = {0};
        buf[0] = 0x05;
        buf[1] = 0xFF;
        buf[2] = 0x04;
        buf[4] = m_rumble_right;
        buf[5] = m_rumble_left;
        buf[6] = m_led_r;
        buf[7] = m_led_g;
        buf[8] = m_led_b;

        DWORD written = 0;
        ok = WriteFile(m_hid_handle, buf, sizeof(buf), &written, NULL);
        if (!ok && pHidD_SetOutputReport) {
            ok = pHidD_SetOutputReport(m_hid_handle, buf, sizeof(buf));
        }
    } else {
        uint8_t buf[78] = {0};
        buf[0] = 0x11;
        buf[1] = 0xC0 | 0x04;
        buf[3] = 0x07;
        buf[6] = m_rumble_right;
        buf[7] = m_rumble_left;
        buf[8] = m_led_r;
        buf[9] = m_led_g;
        buf[10] = m_led_b;

        uint8_t crc_data[75];
        crc_data[0] = 0xA2;
        std::memcpy(&crc_data[1], buf, 74);
        uint32_t crc = ds4_crc32(crc_data, 75);
        buf[74] = (uint8_t)(crc & 0xFF);
        buf[75] = (uint8_t)((crc >> 8) & 0xFF);
        buf[76] = (uint8_t)((crc >> 16) & 0xFF);
        buf[77] = (uint8_t)((crc >> 24) & 0xFF);

        DWORD written = 0;
        ok = WriteFile(m_hid_handle, buf, sizeof(buf), &written, NULL);
        if (!ok && pHidD_SetOutputReport) {
            ok = pHidD_SetOutputReport(m_hid_handle, buf, sizeof(buf));
        }
    }

    if (!ok) {
        ds4_log("send_hid_output_report failed, closing handle %p (err %lu)\n", m_hid_handle, GetLastError());
        CloseHandle(m_hid_handle);
        m_hid_handle = INVALID_HANDLE_VALUE;
    }
}

void ps4_controller::init() {
    ds4_log("ps4_controller::init() called\n");
    const char* dll_names[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
    for (const char* dll : dll_names) {
        hXInput = LoadLibraryA(dll);
        if (hXInput) {
            pXInputGetState = (XInputGetState_t)(void*)GetProcAddress(hXInput, "XInputGetState");
            pXInputSetState = (XInputSetState_t)(void*)GetProcAddress(hXInput, "XInputSetState");
            if (pXInputGetState && pXInputSetState) {
                break;
            }
        }
    }

    find_and_open_ds4_hid();
}

void ps4_controller::update() {
    update_lightbar_and_hero();

    if (m_rumble_timer > 0.0f) {
        m_rumble_timer -= 0.016f;
        if (m_rumble_timer <= 0.0f) {
            m_rumble_left = 0;
            m_rumble_right = 0;
            if (pXInputSetState) {
                XINPUT_VIBRATION vib;
                vib.wLeftMotorSpeed = 0;
                vib.wRightMotorSpeed = 0;
                pXInputSetState(0, &vib);
            }
        }
    }

    send_hid_output_report();
}

void ps4_controller::trigger_feeding() {
    m_feeding_timer = 1.5f;
}

void ps4_controller::update_lightbar_and_hero() {
    float dt = 0.016f;
    m_health_pulse_timer += dt;

    bool is_venom = false;
    float cur_hp = -1.0f;
    float max_hp = -1.0f;

    if (g_world_ptr != nullptr) {
        auto *hero_ptr = (actor*)g_world_ptr->get_hero_ptr(0);
        if (hero_ptr != nullptr) {
            if (hero_ptr->m_player_controller != nullptr) {
                if (hero_ptr->m_player_controller->m_hero_type == hero_type_enum::VENOM ||
                    (int)hero_ptr->m_player_controller->m_hero_type == 2) {
                    is_venom = true;
                }
            }

            // Direct pointer check to m_damage_interface
            auto *dmg = hero_ptr->m_damage_interface;
            if (dmg == nullptr) {
                try {
                    dmg = hero_ptr->damage_ifc();
                } catch (...) {}
            }

            if (dmg != nullptr) {
                cur_hp = dmg->field_1FC.field_0[0];
                max_hp = (dmg->field_1FC.field_0[2] > 0.0f) ? dmg->field_1FC.field_0[2] : 200.0f;
            }
        }
    }

    // Fallback 1: Check HUD hero_health
    if ((cur_hp < 0.0f || max_hp < 0.0f) && g_femanager.IGO != nullptr && g_femanager.IGO->hero_health != nullptr) {
        float hud_hp = g_femanager.IGO->hero_health->field_4C;
        float hud_max = g_femanager.IGO->hero_health->field_50;
        if (hud_max > 0.0f) {
            cur_hp = hud_hp;
            max_hp = hud_max;
        }
    }

    // Fallback 2: Check gamefile settings
    if ((cur_hp < 0.0f || max_hp < 0.0f) && g_game_ptr != nullptr && g_game_ptr->gamefile != nullptr) {
        float gf_hp = g_game_ptr->gamefile->field_340.m_hero_health;
        if (gf_hp > 0.0f) {
            cur_hp = gf_hp;
            max_hp = 200.0f;
        }
    }

    if (max_hp <= 0.0f) max_hp = 200.0f;
    if (cur_hp < 0.0f) cur_hp = max_hp;
    if (cur_hp > max_hp) cur_hp = max_hp;

    // Detect Venom feeding (health increasing)
    if (is_venom && m_last_hp > 0.0f && (cur_hp > m_last_hp + 0.05f)) {
        m_feeding_timer = 1.2f; // Keep feeding animation active while gaining HP
    }

    // Automatic vibration on taking damage
    if (m_last_hp > 0.0f && cur_hp < m_last_hp - 1.0f) {
        float dmg_amt = m_last_hp - cur_hp;
        float heavy = std::clamp(dmg_amt / 25.0f, 0.4f, 1.0f);
        vibrate(heavy, 0.7f, 0.25f);
    }
    m_last_hp = cur_hp;

    if (m_feeding_timer > 0.0f) {
        m_feeding_timer -= dt;
        m_feed_blend = std::min(1.0f, m_feed_blend + dt * 4.0f); // Smooth ramp to green
    } else {
        m_feed_blend = std::max(0.0f, m_feed_blend - dt * 2.5f); // Smooth fade out of green
    }

    // Calculate normalized health ratio (0.0 at <=10% HP, 1.0 at 100% HP)
    float health_pct = (max_hp > 0.0f) ? (cur_hp / max_hp) : 1.0f;
    float health_ratio = std::clamp((health_pct - 0.10f) / 0.90f, 0.0f, 1.0f);

    float target_r = 0.0f, target_g = 200.0f, target_b = 255.0f;

    if (is_venom) {
        // Venom:
        // Full HP (100%): Neon Mor RGB(220, 0, 255)
        // Half HP (50%): Bordo RGB(180, 0, 60)
        // Low HP (<=10%): Kırmızı RGB(255, 0, 0)
        if (health_ratio >= 0.5f) {
            float t = (health_ratio - 0.5f) * 2.0f;
            target_r = 180.0f + (220.0f - 180.0f) * t;
            target_g = 0.0f;
            target_b = 60.0f  + (255.0f - 60.0f)  * t;
        } else {
            float t = health_ratio * 2.0f;
            target_r = 255.0f + (180.0f - 255.0f) * t;
            target_g = 0.0f;
            target_b = 0.0f   + (60.0f  - 0.0f)   * t;
        }
    } else {
        // Spider-Man:
        // Full HP (100%): Açık Mavi / Turkuaz RGB(0, 200, 255)
        // Half HP (50%): Açık Mor / Eflatun RGB(160, 50, 200)
        // Low HP (<=10%): Kırmızı RGB(255, 0, 0)
        if (health_ratio >= 0.5f) {
            float t = (health_ratio - 0.5f) * 2.0f;
            target_r = 160.0f + (0.0f   - 160.0f) * t;
            target_g = 50.0f  + (200.0f - 50.0f)  * t;
            target_b = 200.0f + (255.0f - 200.0f) * t;
        } else {
            float t = health_ratio * 2.0f;
            target_r = 255.0f + (160.0f - 255.0f) * t;
            target_g = 0.0f   + (50.0f  - 0.0f)   * t;
            target_b = 0.0f   + (200.0f - 0.0f)   * t;
        }
    }

    // Critical low health heartbeat pulse (when HP <= 15%)
    if (health_pct <= 0.15f) {
        float pulse = (std::sin(m_health_pulse_timer * 9.0f) + 1.0f) * 0.5f;
        float pulse_mult = 0.40f + 0.60f * pulse;
        target_r *= pulse_mult;
    }

    // Venom Feeding: Smooth blend into Bright Green (0, 255, 0)
    if (is_venom && m_feed_blend > 0.0f) {
        float green_r = 0.0f;
        float green_g = 255.0f;
        float green_b = 0.0f;

        target_r = target_r + (green_r - target_r) * m_feed_blend;
        target_g = target_g + (green_g - target_g) * m_feed_blend;
        target_b = target_b + (green_b - target_b) * m_feed_blend;
    }

    // Temporal Smoothing (Smooth frame-by-frame interpolation)
    float smooth_speed = 5.0f;
    m_cur_r += (target_r - m_cur_r) * std::clamp(dt * smooth_speed, 0.0f, 1.0f);
    m_cur_g += (target_g - m_cur_g) * std::clamp(dt * smooth_speed, 0.0f, 1.0f);
    m_cur_b += (target_b - m_cur_b) * std::clamp(dt * smooth_speed, 0.0f, 1.0f);

    set_lightbar(
        (uint8_t)std::clamp(m_cur_r, 0.0f, 255.0f),
        (uint8_t)std::clamp(m_cur_g, 0.0f, 255.0f),
        (uint8_t)std::clamp(m_cur_b, 0.0f, 255.0f)
    );

    static int s_log_tick = 0;
    if (++s_log_tick % 120 == 0) {
        ds4_log("update[%d]: hero=%s hp=%.1f/%.1f -> LED=(%d, %d, %d) feed=%.2f\n",
            s_log_tick, is_venom ? "VENOM" : "SPIDEY", cur_hp, max_hp, m_led_r, m_led_g, m_led_b, m_feed_blend);
    }
}

void ps4_controller::set_lightbar(uint8_t r, uint8_t g, uint8_t b) {
    m_led_r = r;
    m_led_g = g;
    m_led_b = b;
}

void ps4_controller::vibrate(float heavy_motor, float light_motor, float duration_seconds) {
    m_rumble_left = (uint8_t)std::clamp<int>((int)(heavy_motor * 255.0f), 0, 255);
    m_rumble_right = (uint8_t)std::clamp<int>((int)(light_motor * 255.0f), 0, 255);
    m_rumble_timer = duration_seconds;

    if (pXInputSetState) {
        XINPUT_VIBRATION vib;
        vib.wLeftMotorSpeed = (WORD)(std::clamp<float>(heavy_motor, 0.0f, 1.0f) * 65535.0f);
        vib.wRightMotorSpeed = (WORD)(std::clamp<float>(light_motor, 0.0f, 1.0f) * 65535.0f);
        pXInputSetState(0, &vib);
    }

    send_hid_output_report();
}

void ps4_controller::vibrate(const rumble_struct &rumble) {
    float heavy = (rumble.field_0 > 0.0f) ? rumble.field_0 : 0.0f;
    float light = (rumble.field_4 > 0.0f) ? rumble.field_4 : 0.0f;
    float dur   = (rumble.field_14 > 0.0f) ? rumble.field_14 : 0.25f;
    vibrate(heavy, light, dur);
}

void ps4_controller::stop_vibration() {
    m_rumble_left = 0;
    m_rumble_right = 0;
    m_rumble_timer = 0.0f;

    if (pXInputSetState) {
        XINPUT_VIBRATION vib;
        vib.wLeftMotorSpeed = 0;
        vib.wRightMotorSpeed = 0;
        pXInputSetState(0, &vib);
    }

    send_hid_output_report();
}

bool ps4_controller::poll(unsigned int user_index, InputState &pState) {
    if (user_index > 1) {
        return false;
    }

    bool got_input = false;

    // 1. Check XInput first (DS4Windows, Steam Input, Xbox Controller)
    if (pXInputGetState != nullptr) {
        XINPUT_STATE xstate;
        std::memset(&xstate, 0, sizeof(xstate));
        DWORD res = pXInputGetState(user_index, &xstate);
        if (res == 0) {
            got_input = true;
            m_connected = true;
            if (Input::instance() != nullptr) {
                Input::instance()->m_current_connected |= (1 << user_index);
            }

            const auto &pad = xstate.Gamepad;

            // Sticks with Deadzone
            int lx = pad.sThumbLX;
            int ly = pad.sThumbLY;
            if (std::abs(lx) < 7849) lx = 0;
            if (std::abs(ly) < 7849) ly = 0;
            if (std::abs(lx) > std::abs(pState.field_10)) pState.field_10 = lx;
            if (std::abs(ly) > std::abs(pState.field_14)) pState.field_14 = ly;

            int rx = pad.sThumbRX;
            int ry = pad.sThumbRY;
            if (std::abs(rx) < 8689) rx = 0;
            if (std::abs(ry) < 8689) ry = 0;
            if (std::abs(rx) > std::abs(pState.field_18)) pState.field_18 = rx;
            if (std::abs(ry) > std::abs(pState.field_1C)) pState.field_1C = ry;

            // USM PS2 Orijinal Kontroller:
            // Çarpı=Zıpla, Kare=Yumruk, Üçgen=Tekme, Daire=Duvara yapış/Grab/Fırlat
            if (pad.wButtons & XINPUT_GAMEPAD_A) pState.m_jump = 255;           // Çarpı -> Zıpla
            if (pad.wButtons & XINPUT_GAMEPAD_X) pState.m_punch = 255;          // Kare  -> Yumruk
            if (pad.wButtons & XINPUT_GAMEPAD_Y) pState.m_kick = 255;           // Üçgen -> Tekme / Web Tırmanma
            if (pad.wButtons & XINPUT_GAMEPAD_B) pState.m_stick_to_walls = 255; // Daire -> Duvara yapış / Grab

            // L2 = Web Atağı (m_black_button), R2 = Ağ Savurma (m_throw_web)
            // L2+R2 aynı anda = Web Zip
            bool l2 = (pad.bLeftTrigger  > 30);
            bool r2 = (pad.bRightTrigger > 30);

            if (l2 && r2) {
                // L2+R2 birlikte: her ikisini de set et, oyun combo'yu algılar
                pState.m_black_button = 255;
                pState.m_throw_web    = 255;
                pState.field_D        = 255;
            } else {
                if (l2) pState.m_black_button = 255; // L2 tek = Web Atağı
                if (r2) pState.m_throw_web    = 255; // R2 tek = Ağ Savurma (Web Swing)
            }

            // R1 = yedek Web Zip (kolaylık için), L1 = kullanılmıyor (SM2 kalıntısı)
            if (pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) pState.field_D = 255;
            if (pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER)  pState.field_C = 255;

            if (pad.wButtons & XINPUT_GAMEPAD_START) pState.m_flags |= 0x10;
            if (pad.wButtons & XINPUT_GAMEPAD_BACK)  pState.m_flags |= 0x20;
            if (pad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) pState.m_flags |= 0x80;
            if (pad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB)  pState.m_flags |= 0x40;

            if (pad.wButtons & XINPUT_GAMEPAD_DPAD_UP)    pState.m_flags |= 0x01;
            if (pad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN)  pState.m_flags |= 0x02;
            if (pad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT)  pState.m_flags |= 0x04;
            if (pad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) pState.m_flags |= 0x08;
        }
    }

    // 2. DirectInput Fallback (Native PS4 DirectInput connection)
    if (!got_input && Input::instance() != nullptr) {
        if (Input::instance()->field_4EC[user_index]) {
            const auto &dijs = Input::instance()->field_4F8[user_index];

            auto scale_axis = [](LONG val) -> int {
                if (val == 0) return 0;
                if (val >= -1000 && val <= 1000) {
                    return (int)(val * 32.767f);
                }
                return (int)val;
            };

            int raw_lx = scale_axis(dijs.lX);
            int raw_ly = -scale_axis(dijs.lY); // Invert DirectInput Y for Forward (+ = Up/Forward)
            int raw_rx = scale_axis(dijs.lZ);
            int raw_ry = -scale_axis(dijs.lRz); // Invert DirectInput Rz for Camera Up (+ = Up)

            // Deadzones
            if (std::abs(raw_lx) < 7849) raw_lx = 0;
            if (std::abs(raw_ly) < 7849) raw_ly = 0;
            if (std::abs(raw_rx) < 8689) raw_rx = 0;
            if (std::abs(raw_ry) < 8689) raw_ry = 0;

            if (std::abs(raw_lx) > std::abs(pState.field_10)) pState.field_10 = raw_lx;
            if (std::abs(raw_ly) > std::abs(pState.field_14)) pState.field_14 = raw_ly;
            if (std::abs(raw_rx) > std::abs(pState.field_18)) pState.field_18 = raw_rx;
            if (std::abs(raw_ry) > std::abs(pState.field_1C)) pState.field_1C = raw_ry;

            // USM PS2 Orijinal Düzen (DirectInput PS4 buton sıraları):
            // btn[0]=Square, btn[1]=Cross, btn[2]=Circle, btn[3]=Triangle
            if (dijs.rgbButtons[0] & 0x80) pState.m_punch = 255;           // Square   -> Yumruk
            if (dijs.rgbButtons[1] & 0x80) pState.m_jump = 255;            // Cross    -> Zıpla
            if (dijs.rgbButtons[2] & 0x80) pState.m_stick_to_walls = 255;  // Circle   -> Duvara yapış / Grab
            if (dijs.rgbButtons[3] & 0x80) pState.m_kick = 255;            // Triangle -> Tekme / Web Tırmanma

            if (dijs.rgbButtons[4] & 0x80) pState.field_C = 255;           // L1 -> (SM2 kalıntısı)
            if (dijs.rgbButtons[5] & 0x80) pState.field_D = 255;           // R1 -> Web Zip (yedek)

            // L2 = Web Atağı (m_black_button), R2 = Ağ Savurma (m_throw_web)
            // L2+R2 = Web Zip
            bool di_l2 = (dijs.rgbButtons[6] & 0x80) || (dijs.lRx > 100) || (dijs.rglSlider[0] > 100);
            bool di_r2 = (dijs.rgbButtons[7] & 0x80) || (dijs.lRy > 100) || (dijs.rglSlider[1] > 100);

            if (di_l2 && di_r2) {
                pState.m_black_button = 255;
                pState.m_throw_web    = 255;
                pState.field_D        = 255;
            } else {
                if (di_l2) pState.m_black_button = 255; // L2 = Web Atağı
                if (di_r2) pState.m_throw_web    = 255; // R2 = Ağ Savurma
            }

            if (dijs.rgbButtons[9] & 0x80) pState.m_flags |= 0x10;        // Options -> Start
            if ((dijs.rgbButtons[8] & 0x80) || (dijs.rgbButtons[13] & 0x80)) pState.m_flags |= 0x20; // Share / Touchpad -> Map

            if (dijs.rgbButtons[10] & 0x80) pState.m_flags |= 0x40;       // L3
            if (dijs.rgbButtons[11] & 0x80) pState.m_flags |= 0x80;       // R3

            // D-Pad
            DWORD pov = dijs.rgdwPOV[0];
            if (pov != 0xFFFFFFFF && pov <= 36000) {
                if (pov >= 31500 || pov <= 4500)  pState.m_flags |= 0x01; // Up
                if (pov >= 4500  && pov <= 13500) pState.m_flags |= 0x08; // Right
                if (pov >= 13500 && pov <= 22500) pState.m_flags |= 0x02; // Down
                if (pov >= 22500 && pov <= 31500) pState.m_flags |= 0x04; // Left
            }

            got_input = true;
            m_connected = true;
            Input::instance()->m_current_connected |= (1 << user_index);
        }
    }

    update();
    return got_input;
}

void ps4_controller_patch() {
    ps4_controller::instance().init();
}

