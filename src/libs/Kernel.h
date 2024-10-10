/*
      This file is part of Smoothie (http://smoothieware.org/). The motion control part is heavily based on Grbl (https://github.com/simen/grbl).
      Smoothie is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
      Smoothie is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
      You should have received a copy of the GNU General Public License along with Smoothie. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef KERNEL_H
#define KERNEL_H

#include "Module.h"
#include "StreamOutputPool.h"
#include "StepTicker.h"
#include "SlowTicker.h"
#include "Planner.h"
#include "Adc.h"
#include "I2C.h" // mbed.h lib
#include <array>
#include <vector>
#include <string>

#include "FreeRTOS.h"
#include "timers.h"

// 9 WCS offsets
#define MAX_WCS 9UL
//Module manager
class Config;
class Module;
class Conveyor;
class SerialConsole;
class GcodeDispatch;
class Robot;
class PublicData;
class SimpleShell;

enum STATE {
	IDLE    = 0,
	RUN     = 1,
	HOLD    = 2,
	HOME    = 3,
	ALARM   = 4,
	SLEEP   = 5,
	SUSPEND = 6,
	WAIT    = 7
};

enum HALT_REASON {
	// No need to reset when triggered
	MANUAL     				= 1,
	HOME_FAIL  				= 2,
	PROBE_FAIL 				= 3,
	CALIBRATE_FAIL			= 4,
	ATC_HOME_FAIL   		= 5,
	ATC_TOOL_INVALID		= 6,
	ATC_NO_TOOL				= 7,
	ATC_HAS_TOOL			= 8,
	SPINDLE_OVERHEATED 		= 9,
	SOFT_LIMIT				= 10,
	COVER_OPEN				= 11,
	PROBE_INVALID			= 12,
	E_STOP					= 13,
	// Need to reset when triggered
	HARD_LIMIT				= 21,
	MOTOR_ERROR_X			= 22,
	MOTOR_ERROR_Y			= 23,
	MOTOR_ERROR_Z			= 24,
	SPINDLE_STALL			= 25,
	SD_ERROR				= 26,
	// Need to switch off/on the power
	SPINDLE_ALARM			= 41
};

enum ATC_STATE {
	ATC_NONE   		= 0,
	ATC_DROP 		= 1,
	ATC_PICK		= 2,
	ATC_CALIBRATE	= 3,
	ATC_MARGIN		= 4,
	ATC_ZPROBE		= 5,
	ATC_AUTOLEVEL   = 6,
	ATC_DONE		= 9
};

typedef struct {
	float TLO;
	// int TOOL;
	float G54[3];
//	float G54[5*MAX_WCS];
	float REFMZ;
	float TOOLMZ;
	float reserve;
	int TOOL;
    float perm_vars[20];
} EEPROM_data;

class Kernel {
    public:
        Kernel() {};

        ~Kernel() {
            delete this->i2c;
        }

        void init();

        const char* config_override_filename(){ return "/sd/config-override"; }

        void printk(const char* format, ...) __attribute__ ((format(printf, 2, 3)));
        void vprintk(const char* format, va_list args);

        void add_module(Module* module);
        void register_for_event(_EVENT_ENUM id_event, Module *module);
        void call_event(_EVENT_ENUM id_event, void * argument= nullptr);

        bool kernel_has_event(_EVENT_ENUM id_event, Module *module);
        void unregister_for_event(_EVENT_ENUM id_event, Module *module);

        float get_user_var(int var_num);

        bool is_using_leds() const { return use_leds; }
        bool is_halted() const { return halted; }
        bool is_grbl_mode() const { return grbl_mode; }
        bool is_ok_per_line() const { return ok_per_line; }

        void set_feed_hold(bool f) { feed_hold= f; }
        bool get_feed_hold() const { return feed_hold; }
        bool is_feed_hold_enabled() const { return enable_feed_hold; }
        void set_bad_mcu(bool b) { bad_mcu= b; }
        bool is_bad_mcu() const { return bad_mcu; }

        void set_uploading(bool f) { uploading = f; }
        bool is_uploading() const { return uploading; }

        void set_laser_mode(bool f) { laser_mode = f; }
        bool get_laser_mode() const { return laser_mode; }

        void set_vacuum_mode(bool f) { vacuum_mode = f; }
        bool get_vacuum_mode() const { return vacuum_mode; }

        void set_optional_stop_mode(bool f) { optional_stop_mode = f; }
        bool get_optional_stop_mode() const { return optional_stop_mode; }

        void set_sleeping(bool f) { sleeping = f; }
        bool is_sleeping() const { return sleeping; }

        void set_suspending(bool f) { suspending = f; }
        bool is_suspending() const { return suspending; }

        void set_waiting(bool f) { waiting = f; }
        bool is_waiting() const { return waiting; }

        void set_aborted(bool f) { aborted = f; }
        bool is_aborted() const { return aborted; }

        void set_zprobing(bool f) { zprobing = f; }
        bool is_zprobing() const { return zprobing; }

        void set_halt_reason(uint8_t reason) { halt_reason = reason; }
        uint8_t get_halt_reason() const { return halt_reason; }

        void set_atc_state(uint8_t state) { atc_state = state; }
        uint8_t get_atc_state() const { return atc_state; }

        void read_eeprom_data();
        void write_eeprom_data();
        void erase_eeprom_data();

        std::string get_query_string();

        std::string get_diagnose_string();

        // These modules are available to all other modules
        SerialConsole*    serial;
        StreamOutputPool  streams;
        Planner           planner;
        Config*           config;
        SlowTicker        slow_ticker;
        StepTicker        step_ticker;
        Adc               adc;
        std::string       current_path;
        uint32_t          base_stepping_frequency;

        uint8_t get_state();
        uint8_t halt_reason;
        uint8_t atc_state;
        EEPROM_data eeprom_data;
        float local_vars[20];

    private:
        // When a module asks to be called for a specific event ( a hook ), this is where that request is remembered
        mbed::I2C* i2c;
        std::array<std::vector<Module*>, NUMBER_OF_DEFINED_EVENTS> hooks;
        struct {
            bool use_leds:1;
            bool halted:1;
            bool grbl_mode:1;
            bool feed_hold:1;
            bool ok_per_line:1;
            volatile bool enable_feed_hold:1;
            bool bad_mcu:1;
            volatile bool uploading:1;
            bool laser_mode:1;
            bool vacuum_mode:1;
            bool optional_stop_mode:1;
            bool sleeping:1;
            bool suspending: 1;
            bool waiting: 1;
            bool aborted: 1;
            bool zprobing:1;
        };
        int iic_page_write(unsigned char u8PageNum, unsigned char u8len, unsigned char *pu8Array);

};

extern Kernel THEKERNEL;
extern Conveyor THECONVEYOR;
extern Robot THEROBOT;
extern SimpleShell simpleshell;
extern GcodeDispatch gcode_dispatch;    

#endif
