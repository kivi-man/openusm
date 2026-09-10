#pragma once

#include <windows.h>
#include <cstdint>
#include "pc_joypad_device.h"

struct rumble_struct;

class ps4_controller {
public:
    static ps4_controller& instance();

    void init();
    void update();
    bool poll(unsigned int user_index, InputState &pState);
    void vibrate(float heavy_motor, float light_motor, float duration_seconds);
    void vibrate(const rumble_struct &rumble);
    void stop_vibration();
    void trigger_feeding();
    void set_lightbar(uint8_t r, uint8_t g, uint8_t b);
    bool is_ps4_active() const {
        return m_connected || (m_hid_handle != INVALID_HANDLE_VALUE && m_hid_handle != NULL);
    }

private:
    ps4_controller();
    ~ps4_controller();

    void find_and_open_ds4_hid();
    void send_hid_output_report();
    void update_lightbar_and_hero();

    HANDLE m_hid_handle;
    bool m_is_bluetooth;
    bool m_connected;
    DWORD m_last_hid_search_tick;

    // LED state
    uint8_t m_led_r;
    uint8_t m_led_g;
    uint8_t m_led_b;

    // Rumble state
    uint8_t m_rumble_left;
    uint8_t m_rumble_right;
    float m_rumble_timer;

    // Last sent state for rate-limiting
    uint8_t m_last_sent_r;
    uint8_t m_last_sent_g;
    uint8_t m_last_sent_b;
    uint8_t m_last_sent_rl;
    uint8_t m_last_sent_rr;
    DWORD m_last_send_tick;

    // Pulse timer for low health
    float m_health_pulse_timer;

    // Smooth color animation & feeding state
    float m_cur_r;
    float m_cur_g;
    float m_cur_b;
    float m_last_hp;
    float m_feed_blend;
    float m_feeding_timer;
};

extern void ps4_controller_patch();

