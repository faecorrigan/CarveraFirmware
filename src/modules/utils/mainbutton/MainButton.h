#ifndef MAINBUTTON_H
#define MAINBUTTON_H

#include "libs/Pin.h"

class MainButton : public Module {
    public:
		MainButton();
        void on_module_loaded();
        void on_idle(void *argument);
        uint32_t button_tick(uint32_t dummy);

    private:
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

        uint8_t hold_toggle;
        uint8_t led_update_timer;
        uint32_t button_press_time;
        uint32_t long_press_time_ms;

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
};

#endif
