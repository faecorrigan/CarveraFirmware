/*
      This file is part of Smoothie (http://smoothieware.org/). The motion control part is heavily based on Grbl (https://github.com/simen/grbl).
      Smoothie is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
      Smoothie is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
      You should have received a copy of the GNU General Public License along with Smoothie. If not, see <http://www.gnu.org/licenses/>.
*/

#include "libs/Kernel.h"

#include "modules/tools/laser/Laser.h"
#include "modules/tools/spindle/SpindleMaker.h"
#include "modules/tools/temperaturecontrol/TemperatureControlPool.h"
#include "modules/tools/endstops/Endstops.h"
#include "modules/tools/zprobe/ZProbe.h"
#include "modules/tools/scaracal/SCARAcal.h"
#include "RotaryDeltaCalibration.h"
#include "modules/tools/switch/SwitchPool.h"
#include "modules/tools/temperatureswitch/TemperatureSwitch.h"
#include "modules/tools/drillingcycles/Drillingcycles.h"
#include "modules/tools/atc/ATCHandler.h"
#include "modules/utils/wifi/WifiProvider.h"
#include "modules/utils/webserver/WebServer.h"
#include "modules/robot/Conveyor.h"
#include "modules/robot/Robot.h"
#include "modules/utils/simpleshell/SimpleShell.h"
#include "modules/utils/configurator/Configurator.h"
#include "modules/utils/player/Player.h"
#include "modules/utils/mainbutton/MainButton.h"
#include "modules/communication/GcodeDispatch.h"
#include "modules/communication/WirelessProbe.h"
#include "Config.h"
#include "checksumm.h"
#include "ConfigValue.h"
#include "Robot.h"

// #include "libs/ChaNFSSD/SDFileSystem.h"
#include "libs/nuts_bolts.h"
#include "libs/utils.h"

// Debug
#include "libs/SerialMessage.h"

//#include "libs/USBDevice/SDCard/SDCard.h"
#include "libs/USBDevice/SDCard/SDFileSystem.h"
// #include "libs/USBDevice/USBSerial/USBSerial.h"
// #include "libs/USBDevice/DFU.h"
#include "libs/SDFAT.h"
#include "Logging.h"
#include "ToolManager.h"

#include "libs/Watchdog.h"

#include "version.h"
#include "system_LPC17xx.h"
#include "platform_memory.h"

#include "mbed.h"

// disable MSD
#define DISABLEMSD
#define second_usb_serial_enable_checksum  CHECKSUM("second_usb_serial_enable")
#define watchdog_timeout_checksum  CHECKSUM("watchdog_timeout")

SDFileSystem sd __attribute__ ((section ("AHBSRAM0"))) (P0_18, P0_17, P0_15, P0_16, 12000000);

SDFAT mounter __attribute__ ((section ("AHBSRAM0"))) ("sd", &sd);

GPIO leds[4] = {
    GPIO(P4_29),
    GPIO(P4_28),
	GPIO(P0_4),
    GPIO(P1_17)
};

Kernel THEKERNEL __attribute__((section("AHBSRAM0")));
Conveyor THECONVEYOR __attribute__((section("AHBSRAM1")));
Robot THEROBOT __attribute__((section("AHBSRAM1")));
GcodeDispatch gcode_dispatch __attribute__((section("AHBSRAM1")));
SimpleShell simpleshell __attribute__((section("AHBSRAM1")));
WifiProvider wifi_provider __attribute__((section("AHBSRAM1")));
WebServer web_server __attribute__((section("AHBSRAM1"))) (&wifi_provider);

Player player __attribute__((section("AHBSRAM0")));
WirelessProbe wireless_probe __attribute__((section("AHBSRAM0")));

void init() {
    // Default pins to low status
    for (int i = 0; i < 4; i++){
        leds[i].output();
        leds[i]= 0;
    }

    THEKERNEL.init();

    THECONVEYOR.init();
    THEKERNEL.add_module(&THECONVEYOR);

    gcode_dispatch.init();
    THEKERNEL.add_module(&gcode_dispatch);

    THEROBOT.init();
    THEKERNEL.add_module(&THEROBOT);

    THEKERNEL.add_module(&simpleshell);

    printk("Smoothie Running @%ldMHz\r\n", SystemCoreClock / 1000000);
    simpleshell.version_command("", &THEKERNEL.streams);

    bool sdok = (sd.disk_initialize() == 0);
    if(!sdok) printk("SDCard failed to initialize\r\n");

    #ifdef NONETWORK
        printk("NETWORK is disabled\r\n");
    #endif

    // Create and add main modules
    THEKERNEL.add_module(&player);
    THEKERNEL.add_module( new(AHB0) ATCHandler() );
    THEKERNEL.add_module(&wireless_probe);
    THEKERNEL.add_module( new(AHB0) MainButton() );

    wifi_provider.init();
    THEKERNEL.add_module(&wifi_provider);
    THEKERNEL.add_module(&web_server);

    // these modules can be completely disabled in the Makefile by adding to EXCLUDE_MODULES
    #ifndef NO_TOOLS_SWITCH
    SwitchPool *sp= new SwitchPool();
    sp->load_tools();
    delete sp;
    #endif

    #ifndef NO_TOOLS_EXTRUDER
    // NOTE this must be done first before Temperature control so ToolManager can handle Tn before temperaturecontrol module does
    ExtruderMaker *em= new(AHB0) ExtruderMaker();
    em->load_tools();
    delete em;
    #endif

    // #ifndef NO_TOOLS_TEMPERATURECONTROL
    // Note order is important here must be after extruder so Tn as a parameter will get executed first
    TemperatureControlPool *tp= new(AHB0) TemperatureControlPool();
    tp->load_tools();
    delete tp;

    // #endif
    #ifndef NO_TOOLS_ENDSTOPS
    THEKERNEL.add_module( new(AHB0) Endstops() );
    #endif
    #ifndef NO_TOOLS_LASER
    THEKERNEL.add_module( new(AHB0) Laser() );
    #endif

    #ifndef NO_TOOLS_SPINDLE
    SpindleMaker *sm = new(AHB0) SpindleMaker();
    sm->load_spindle();
    delete sm;
    #endif
    #ifndef NO_TOOLS_ZPROBE
    THEKERNEL.add_module( new(AHB0) ZProbe() );
    #endif
    #ifndef NO_TOOLS_SCARACAL
    THEKERNEL.add_module( new(AHB0) SCARAcal() );
    #endif
    #ifndef NO_TOOLS_ROTARYDELTACALIBRATION
    THEKERNEL.add_module( new(AHB0) RotaryDeltaCalibration() );
    #endif
    #ifndef NO_TOOLS_TEMPERATURESWITCH
    // Must be loaded after TemperatureControl
    THEKERNEL.add_module( new(AHB0) TemperatureSwitch() );
    #endif
    #ifndef NO_TOOLS_DRILLINGCYCLES
    THEKERNEL.add_module( new(AHB0) Drillingcycles() );
    #endif


    // 10 second watchdog timeout (or config as seconds)
    float t= THEKERNEL.config->value( watchdog_timeout_checksum )->by_default(10.0F)->as_number();
    if(t > 0.1F) {
        // NOTE setting WDT_RESET with the current bootloader would leave it in DFU mode which would be suboptimal
        THEKERNEL.add_module( new(AHB0) Watchdog(t * 1000000, WDT_RESET )); // WDT_RESET));
        printk("Watchdog enabled for %1.3f seconds\n", t);
    }else{
        printk("WARNING Watchdog is disabled\n");
    }

    // clear up the config cache to save some memory
    THEKERNEL.config->config_cache_clear();

    if(THEKERNEL.is_using_leds()) {
        // set some leds to indicate status... led0 init done, led1 mainloop running, led2 idle loop running, led3 sdcard ok
        leds[0]= 1; // indicate we are done with init
        leds[3]= sdok?1:0; // 4th led indicates sdcard is available (TODO maye should indicate config was found)
    }

    if(sdok) {
        // load config override file if present
        // NOTE only Mxxx commands that set values should be put in this file. The file is generated by M500
        FILE *fp= fopen(THEKERNEL.config_override_filename(), "r");
        if(fp != NULL) {
            char buf[132];
            printk("Loading config override file: %s...\n", THEKERNEL.config_override_filename());
            while(fgets(buf, sizeof buf, fp) != NULL) {
                printk("  %s", buf);
                if(buf[0] == ';') continue; // skip the comments
                struct SerialMessage message= {&(StreamOutput::NullStream), buf, 0};
                THEKERNEL.call_event(ON_CONSOLE_LINE_RECEIVED, &message);
            }
            printk("config override file executed\n");
            fclose(fp);
        }
    }

    // start the timers and interrupts
    THECONVEYOR.start(THEROBOT.get_number_registered_motors());
    
    THEKERNEL.step_ticker.start();
    THEKERNEL.slow_ticker.start();
}

int main()
{
    init();

    uint16_t cnt= 0;
    // Main loop
    while(1){
        if(THEKERNEL.is_using_leds()) {
            // flash led 2 to show we are alive
            leds[1]= (cnt++ & 0x1000) ? 1 : 0;
        }
        THEKERNEL.call_event(ON_MAIN_LOOP);
        THEKERNEL.call_event(ON_IDLE);
    }
}
