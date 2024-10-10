#ifndef MAINBUTTON_H
#define MAINBUTTON_H

#include "libs/Pin.h"
#include "mbed.h"
#include "SoftTimer.h"

class MainButton : public Module {
    public:
        MainButton()
        : timer("MainButtonTimer", 1000, true, this, &MainButton::button_tick)
        {}

        void on_module_loaded();
        void on_idle(void *argument);
        void button_tick();
        void on_second_tick(void *);
        void on_get_public_data(void* argument);
        void on_set_public_data(void* argument);

    private:
        SoftTimer timer;

        Pin main_button;
        Pin main_button_LED_R;
        Pin main_button_LED_G;
        Pin main_button_LED_B;
        enum BUTTON_STATE {
            NONE,
			BUTTON_LONG_PRESSED,
			BUTTON_SHORT_PRESSED,
			BUTTON_LED_UPDATE
        };

        Pin e_stop;
        Pin PS12;
        Pin PS24;

        uint16_t power_fan_delay_s;
        uint32_t power_fan_countdown_us;

        uint8_t hold_toggle;
        uint8_t led_update_timer;
        uint32_t button_press_time;
        uint32_t long_press_time_ms;
        std::string long_press_enable;

        bool auto_sleep;
        uint8_t auto_sleep_min;
        uint32_t sleep_countdown_us;

        bool enable_light;
        uint8_t turn_off_light_min;
        uint32_t light_countdown_us;

        bool button_pressed;
        volatile BUTTON_STATE button_state;
        
        bool stop_on_cover_open;

        uint32_t poll_frequency;

        bool sd_ok;
        bool using_12v;

        void e_stop_irq();
        void switch_power_12(int state);
        void switch_power_24(int state);
};

#endif
