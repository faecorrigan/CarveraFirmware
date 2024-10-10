/*
      This file is part of Smoothie (http://smoothieware.org/). The motion control part is heavily based on Grbl (https://github.com/simen/grbl).
      Smoothie is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
      Smoothie is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
      You should have received a copy of the GNU General Public License along with Smoothie. If not, see <http://www.gnu.org/licenses/>.
*/

#include <string>
#include <stdarg.h>
using std::string;
#include "libs/Module.h"
#include "libs/Kernel.h"
#include "Gcode.h"
#include "Config.h"
#include "ConfigValue.h"
#include "libs/nuts_bolts.h"
#include "WirelessProbe.h"
#include "libs/RingBuffer.h"
#include "libs/SerialMessage.h"
#include "PublicDataRequest.h"
#include "PublicData.h"
#include "libs/Logging.h"
#include "libs/StreamOutput.h"
#include "SwitchPublicAccess.h"
#include "ATCHandlerPublicAccess.h"

#define wp_checksum						CHECKSUM("wp")
#define min_voltage_checksum			CHECKSUM("min_voltage")
#define max_voltage_checksum			CHECKSUM("max_voltage")
#define baud_rate_setting_checksum 		CHECKSUM("baud_rate")
#define uart_checksum              		CHECKSUM("uart")


// Wireless probe serial reading module
// Treats every received line as a command and passes it ( via event call ) to the command dispatcher.
// The command dispatcher will then ask other modules if they can do something with it

// Called when the module has just been loaded
void WirelessProbe::on_module_loaded() {
    this->wp_voltage = 0.0;

	this->serial = new mbed::Serial( USBTX, USBRX );
    this->serial->baud(THEKERNEL.config->value(uart_checksum, baud_rate_setting_checksum)->by_default(DEFAULT_SERIAL_BAUD_RATE)->as_number());

    // We want to be called every time a new char is received
    this->serial->attach(this, &WirelessProbe::on_serial_char_received, mbed::Serial::RxIrq);

    this->min_voltage = THEKERNEL.config->value(wp_checksum, min_voltage_checksum)->by_default(3.6F)->as_number();
    this->max_voltage = THEKERNEL.config->value(wp_checksum, max_voltage_checksum)->by_default(4.1F)->as_number();

    // We only call the command dispatcher in the main loop, nowhere else
    this->register_for_event(ON_MAIN_LOOP);
    this->register_for_event(ON_GET_PUBLIC_DATA);
    this->register_for_event(ON_SET_PUBLIC_DATA);
    this->register_for_event(ON_GCODE_RECEIVED);
}


// Called on Serial::RxIrq interrupt, meaning we have received a char
void WirelessProbe::on_serial_char_received() {
    while (this->serial->readable()){
        char received = this->serial->getc();
        // convert CR to NL (for host OSs that don't send NL)
        if ( received == '\r' ) { received = '\n'; }
        this->buffer.push_back(received);
    }
}

// Actual event calling must happen in the main loop because if it happens in the interrupt we will loose data
void WirelessProbe::on_main_loop(void * argument) {
    if ( this->has_char('\n') ) {
        string received;
        received.reserve(20);
        while (1) {
           char c;
           this->buffer.pop_front(c);
           if ( c == '\n' ) {
        	   // printk("WP received: [%s]\n", received.c_str());
        	   if (received[0] == 'V') {
            	   // get wireless probe voltage
            	   Gcode gc(received, &StreamOutput::NullStream);
            	   if (gc.get_value('V') <= 4.2) {
                	   this->wp_voltage = gc.get_value('V');
                	   // compare voltage value and switch probe charger
                	   if (this->wp_voltage <= this->min_voltage) {
                		   struct pad_switch pad;
                           bool ok = PublicData::get_value(switch_checksum, probecharger_checksum, 0, &pad);
                           if (!ok || !pad.state) {
                        	   if (!THEKERNEL.is_uploading())
                        		   printk("WP voltage: [%1.2fV], start charging\n", this->wp_voltage);
                    		   bool b = true;
                    		   PublicData::set_value( switch_checksum, probecharger_checksum, state_checksum, &b );
                           }
                	   } else if (this->wp_voltage >= this->max_voltage) {
                		   struct pad_switch pad;
                           bool ok = PublicData::get_value(switch_checksum, probecharger_checksum, 0, &pad);
                           if (!ok || pad.state) {
                        	   if (!THEKERNEL.is_uploading())
                        		   printk("WP voltage: [%1.2fV], end charging\n", this->wp_voltage);
                    		   bool b = false;
                    		   PublicData::set_value( switch_checksum, probecharger_checksum, state_checksum, &b );
                           }
                	   }
            	   }
        	   } else if (received[0] == 'A' && received.length() > 2) {
        		   // get wireless probe address
        		   uint16_t probe_addr = ((uint16_t)received[2] << 8) | received[1];
        		   printk("WP power: [%1.2fv], addr: [%0d]\n", this->wp_voltage, probe_addr);
        	   } else if (received[0] == 'P' && received.length() > 1) {
        		   printk("WP PAIR %s!\n", received[1] ? "SUCCESS" : "TIMEOUT");
        	   }
               return;
            } else {
                received += c;
            }
        }
    }
}

int WirelessProbe::puts(const char* s)
{
    //return fwrite(s, strlen(s), 1, (FILE*)(*this->serial));
    size_t n= strlen(s);
    for (size_t i = 0; i < n; ++i) {
        putc(s[i]);
    }
    return n;
}

int WirelessProbe::gets(char** buf)
{
	getc_result = this->getc();
	*buf = &getc_result;
	return 1;
}

int WirelessProbe::putc(int c)
{
    return this->serial->putc(c);
}

int WirelessProbe::getc()
{
    return this->serial->getc();
}

// Does the queue have a given char ?
bool WirelessProbe::has_char(char letter){
    int index = this->buffer.tail;
    while( index != this->buffer.head ){
        if( this->buffer.buffer[index] == letter ){
            return true;
        }
        index = this->buffer.next_block_index(index);
    }
    return false;
}

void WirelessProbe::on_get_public_data(void *argument) {
    PublicDataRequest* pdr = static_cast<PublicDataRequest*>(argument);

    if(!pdr->starts_with(atc_handler_checksum)) return;

    if(pdr->second_element_is(get_wp_voltage_checksum)) {
        float *t = static_cast<float*>(pdr->get_data_ptr());
        *t = this->wp_voltage;
        pdr->set_taken();
    } else if(pdr->second_element_is(show_wp_state_checksum)) {
    	this->putc('Q');
        pdr->set_taken();
    }


}

void WirelessProbe::on_set_public_data(void *argument) {
    PublicDataRequest* pdr = static_cast<PublicDataRequest*>(argument);

    if(!pdr->starts_with(atc_handler_checksum)) return;

    if(pdr->second_element_is(set_wp_laser_checksum)) {
    	this->putc('L');
        pdr->set_taken();
    }
}

void WirelessProbe::on_gcode_received(void *argument)
{
    Gcode *gcode = static_cast<Gcode*>(argument);

    if (gcode->has_m) {
    	if (gcode->m == 470) {
    		if (gcode->has_letter('S')) {
        		uint16_t new_addr = gcode->get_value('S');
        		printk("Change WP address to: [%d]\n", new_addr);
        		this->putc('S');
                this->putc(new_addr & 0xff);
                this->putc(new_addr >> 8);
                this->putc('#');
    		}
    	} else if (gcode->m == 471) {
    		printk("Set WP into pairing mode...\n");
    		this->putc('P');
    	} else if (gcode->m == 472) {
    		printk("Open WP Laser...\n");
    		this->putc('L');
    	} else if (gcode->m == 881) {
    		if (gcode->has_letter('S')) {
        		uint16_t channel = gcode->get_value('S');
        		printk("Set 2.4G Channel to: [%d] and start trans...\n", channel);
        		this->putc(channel);
    		}
    	} else if (gcode->m == 882) {
    		printk("Stop 2.4G transmission...\n");
    		this->putc(27);
    	}
    }
}
