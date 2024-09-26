/*
    This file is part of Smoothie (http://smoothieware.org/). The motion control part is heavily based on Grbl (https://github.com/simen/grbl).
    Smoothie is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
    Smoothie is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
    You should have received a copy of the GNU General Public License along with Smoothie. If not, see <http://www.gnu.org/licenses/>.
*/


#include "SimpleShell.h"

#include "rtc_time.h"
#include "../mainbutton/MainButtonPublicAccess.h"
#include "libs/Kernel.h"
#include "libs/nuts_bolts.h"
#include "libs/utils.h"
#include "libs/SerialMessage.h"
#include "libs/StreamOutput.h"
#include "libs/Logging.h"
#include "Conveyor.h"
#include "DirHandle.h"
#include "mri.h"
#include "version.h"
#include "PublicDataRequest.h"
#include "AppendFileStream.h"
#include "FileStream.h"
#include "checksumm.h"
#include "PublicData.h"
#include "Gcode.h"
#include "Robot.h"
#include "ToolManagerPublicAccess.h"
#include "GcodeDispatch.h"
#include "BaseSolution.h"
#include "StepperMotor.h"
#include "Configurator.h"
#include "Block.h"
#include "SpindlePublicAccess.h"
#include "ZProbePublicAccess.h"
#include "LaserPublicAccess.h"
#include "TemperatureControlPublicAccess.h"
#include "EndstopsPublicAccess.h"
#include "ATCHandlerPublicAccess.h"
// #include "NetworkPublicAccess.h"
#include "platform_memory.h"
#include "SwitchPublicAccess.h"
#include "SDFAT.h"
#include "Thermistor.h"
#include "md5.h"
#include "utils.h"
#include "AutoPushPop.h"
#include "MainButtonPublicAccess.h"
#include "system_LPC17xx.h"
#include "LPC17xx.h"
#include "WifiPublicAccess.h"
#include "md5.h"
#include "quicklz.h"

#include "mbed.h" // for wait_ms()

extern unsigned int g_maximumHeapAddress;

// used for XMODEM
#define SOH  0x01
#define STX  0x02
#define EOT  0x04
#define ACK  0x06
#define NAK  0x15
#define CAN  0x16 //0x18
#define CTRLZ 0x1A

#include <malloc.h>
#include <mri.h>
#include <stdio.h>
#include <stdint.h>
#include <functional>

extern "C" uint32_t  __end__;
extern "C" uint32_t  __malloc_free_list;
extern "C" uint32_t  _sbrk(int size);

unsigned char xbuff[8200] __attribute__((section("AHBSRAM1"))); /* 2 for data length, 8192 for XModem + 3 head chars + 2 crc + nul */
unsigned char fbuff[4096] __attribute__((section("AHBSRAM1")));

#define MAXRETRANS 10
#define TIMEOUT_MS 100

// support upload file type definition
#define FILETYPE	"lz"		//compressed by quicklz
// version definition
#define VERSION "0.9.8"

// command lookup table
const SimpleShell::ptentry_t SimpleShell::commands_table[] = {
    {"ls",       &SimpleShell::ls_command},
    {"cd",       &SimpleShell::cd_command},
    {"pwd",      &SimpleShell::pwd_command},
    {"cat",      &SimpleShell::cat_command},
    {"echo",     &SimpleShell::echo_command},
    {"rm",       &SimpleShell::rm_command},
    {"mv",       &SimpleShell::mv_command},
    {"mkdir",    &SimpleShell::mkdir_command},
    {"upload",   &SimpleShell::upload_command},
	{"download", &SimpleShell::download_command},
    {"reset",    &SimpleShell::reset_command},
    {"dfu",      &SimpleShell::dfu_command},
    {"break",    &SimpleShell::break_command},
    {"help",     &SimpleShell::help_command},
    {"?",        &SimpleShell::help_command},
	{"ftype",	 &SimpleShell::ftype_command},
    {"version",  &SimpleShell::version_command},
    {"mem",      &SimpleShell::mem_command},
    {"get",      &SimpleShell::get_command},
    {"set_temp", &SimpleShell::set_temp_command},
    {"switch",   &SimpleShell::switch_command},
    {"net",      &SimpleShell::net_command},
	{"ap",     &SimpleShell::ap_command},
	{"wlan",     &SimpleShell::wlan_command},
	{"diagnose",   &SimpleShell::diagnose_command},
	{"sleep",   &SimpleShell::sleep_command},
	{"power",   &SimpleShell::power_command},
    {"load",     &SimpleShell::load_command},
    {"save",     &SimpleShell::save_command},
    {"remount",  &SimpleShell::remount_command},
    {"calc_thermistor", &SimpleShell::calc_thermistor_command},
    {"thermistors", &SimpleShell::print_thermistors_command},
    {"md5sum",   &SimpleShell::md5sum_command},
	{"time",   &SimpleShell::time_command},
    {"test",     &SimpleShell::test_command},

    // unknown command
    {NULL, NULL}
};

int SimpleShell::reset_delay_secs = 0;

// Adam Greens heap walk from http://mbed.org/forum/mbed/topic/2701/?page=4#comment-22556
static uint32_t heapWalk(StreamOutput *stream, bool verbose)
{
    uint32_t chunkNumber = 1;
    // The __end__ linker symbol points to the beginning of the heap.
    uint32_t chunkCurr = (uint32_t)&__end__;
    // __malloc_free_list is the head pointer to newlib-nano's link list of free chunks.
    uint32_t freeCurr = __malloc_free_list;
    // Calling _sbrk() with 0 reserves no more memory but it returns the current top of heap.
    uint32_t heapEnd = _sbrk(0);
    // accumulate totals
    uint32_t freeSize = 0;
    uint32_t usedSize = 0;

    stream->printf("Used Heap Size: %lu\n", heapEnd - chunkCurr);

    // Walk through the chunks until we hit the end of the heap.
    while (chunkCurr < heapEnd) {
        // Assume the chunk is in use.  Will update later.
        int      isChunkFree = 0;
        // The first 32-bit word in a chunk is the size of the allocation.  newlib-nano over allocates by 8 bytes.
        // 4 bytes for this 32-bit chunk size and another 4 bytes to allow for 8 byte-alignment of returned pointer.
        uint32_t chunkSize = *(uint32_t *)chunkCurr;
        // The start of the next chunk is right after the end of this one.
        uint32_t chunkNext = chunkCurr + chunkSize;

        // The free list is sorted by address.
        // Check to see if we have found the next free chunk in the heap.
        if (chunkCurr == freeCurr) {
            // Chunk is free so flag it as such.
            isChunkFree = 1;
            // The second 32-bit word in a free chunk is a pointer to the next free chunk (again sorted by address).
            freeCurr = *(uint32_t *)(freeCurr + 4);
        }

        // Skip past the 32-bit size field in the chunk header.
        chunkCurr += 4;
        // 8-byte align the data pointer.
        chunkCurr = (chunkCurr + 7) & ~7;
        // newlib-nano over allocates by 8 bytes, 4 bytes for the 32-bit chunk size and another 4 bytes to allow for 8
        // byte-alignment of the returned pointer.
        chunkSize -= 8;
        if (verbose)
            stream->printf("  Chunk: %lu  Address: 0x%08lX  Size: %lu  %s\n", chunkNumber, chunkCurr, chunkSize, isChunkFree ? "CHUNK FREE" : "");

        if (isChunkFree) freeSize += chunkSize;
        else usedSize += chunkSize;

        chunkCurr = chunkNext;
        chunkNumber++;
    }
    stream->printf("Allocated: %lu, Free: %lu\r\n", usedSize, freeSize);
    return freeSize;
}


void SimpleShell::on_module_loaded()
{
    this->register_for_event(ON_CONSOLE_LINE_RECEIVED);
    this->register_for_event(ON_GCODE_RECEIVED);
    this->register_for_event(ON_SECOND_TICK);

    reset_delay_secs = 0;
}

void SimpleShell::on_second_tick(void *)
{
    // we are timing out for the reset
    if (reset_delay_secs > 0) {
        if (--reset_delay_secs == 0) {
            system_reset(false);
        }
    }
}

void SimpleShell::on_gcode_received(void *argument)
{
    Gcode *gcode = static_cast<Gcode *>(argument);
    string args = get_arguments(gcode->get_command());

    if (gcode->has_m) {
        if (gcode->m == 20) { // list sd card
            gcode->stream->printf("Begin file list\r\n");
            ls_command("/sd", gcode->stream);
            gcode->stream->printf("End file list\r\n");

        } else if (gcode->m == 30) { // remove file
            if(!args.empty() && !THEKERNEL->is_grbl_mode())
                rm_command("/sd/" + args, gcode->stream);
        } else if (gcode->m == 331) { // change to vacuum mode
			THEKERNEL->set_vacuum_mode(true);
		    // get spindle state
		    struct spindle_status ss;
		    bool ok = PublicData::get_value(pwm_spindle_control_checksum, get_spindle_status_checksum, &ss);
		    if (ok) {
		    	if (ss.state) {
	        		// open vacuum
	        		bool b = true;
	                PublicData::set_value( switch_checksum, vacuum_checksum, state_checksum, &b );

		    	}
        	}
		    // turn on vacuum mode
			gcode->stream->printf("turning vacuum mode on\r\n");
		} else if (gcode->m == 332) { // change to CNC mode
			THEKERNEL->set_vacuum_mode(false);
		    // get spindle state
		    struct spindle_status ss;
		    bool ok = PublicData::get_value(pwm_spindle_control_checksum, get_spindle_status_checksum, &ss);
		    if (ok) {
		    	if (ss.state) {
	        		// close vacuum
	        		bool b = false;
	                PublicData::set_value( switch_checksum, vacuum_checksum, state_checksum, &b );

		    	}
        	}
			// turn off vacuum mode
			gcode->stream->printf("turning vacuum mode off\r\n");

		} else if (gcode->m == 333) { // turn off optional stop mode
			THEKERNEL->set_optional_stop_mode(false);
			// turn off optional stop mode
			gcode->stream->printf("turning optional stop mode off\r\n");
		} else if (gcode->m == 334) { // turn off optional stop mode
			THEKERNEL->set_optional_stop_mode(true);
			// turn on optional stop mode
			gcode->stream->printf("turning optional stop mode on\r\n");
		}

        
    }
}

bool SimpleShell::parse_command(const char *cmd, std::string args, StreamOutput *stream)
{
    for (const ptentry_t *p = commands_table; p->command != nullptr; ++p) {
        if (strncasecmp(cmd, p->name, strlen(p->name)) == 0) {
            (this->*(p->command))(args, stream);
            return true;
        }
    }
    return false;
}

// When a new line is received, check if it is a command, and if it is, act upon it
void SimpleShell::on_console_line_received( void *argument )
{
    SerialMessage new_message = *static_cast<SerialMessage *>(argument);
    string possible_command = new_message.message;

    // ignore anything that is not lowercase or a $ as it is not a command
    if(possible_command.size() == 0 || (!islower(possible_command[0]) && possible_command[0] != '$')) {
        return;
    }

    // it is a grbl compatible command
    if(possible_command[0] == '$' && possible_command.size() >= 2) {
        switch(possible_command[1]) {
            case 'G':
                // issue get state
                get_command("state", new_message.stream);
                new_message.stream->printf("ok\n");
                break;

            case 'I':
                // issue get state for smoopi
                get_command("state", new_message.stream);
                break;

            case 'X':
                if(THEKERNEL->is_halted()) {
                    THEKERNEL->call_event(ON_HALT, (void *)1); // clears on_halt
                    new_message.stream->printf("[Caution: Unlocked]\nok\n");
                }
                break;

            case '#':
                grblDP_command("", new_message.stream);
                new_message.stream->printf("ok\n");
                break;

            case 'H':
                if(THEKERNEL->is_halted()) THEKERNEL->call_event(ON_HALT, (void *)1); // clears on_halt
                if(THEKERNEL->is_grbl_mode()) {
                    // issue G28.2 which is force homing cycle
                    Gcode gcode("G28.2", new_message.stream);
                    THEKERNEL->call_event(ON_GCODE_RECEIVED, &gcode);
                }else{
                    Gcode gcode("G28", new_message.stream);
                    THEKERNEL->call_event(ON_GCODE_RECEIVED, &gcode);
                }
                new_message.stream->printf("ok\n");
                break;

            case 'S':
                switch_command(possible_command, new_message.stream);
                break;

            case 'J':
                // instant jog command
                jog(possible_command, new_message.stream);
                break;

            default:
                new_message.stream->printf("error:Invalid statement\n");
                break;
        }

    }else{

        //new_message.stream->printf("Received %s\r\n", possible_command.c_str());
        string cmd = shift_parameter(possible_command);

        // Configurator commands
        if (cmd == "config-get"){
            THEKERNEL->configurator->config_get_command(  possible_command, new_message.stream );

        } else if (cmd == "config-set"){
            THEKERNEL->configurator->config_set_command(  possible_command, new_message.stream );

        } else if (cmd == "config-load"){
            THEKERNEL->configurator->config_load_command(  possible_command, new_message.stream );

        } else if (cmd == "config-get-all"){
            config_get_all_command(  possible_command, new_message.stream );

        } else if (cmd == "config-restore"){
            config_restore_command(  possible_command, new_message.stream );

        } else if (cmd == "config-default"){
            config_default_command(  possible_command, new_message.stream );

        } else if (cmd == "play" || cmd == "progress" || cmd == "abort" || cmd == "suspend"
        		|| cmd == "resume" || cmd == "buffer" || cmd == "goto") {
            // these are handled by Player module

        } else if (cmd == "laser") {
            // these are handled by Laser module

        } else if (cmd.substr(0, 2) == "ok") {
            // probably an echo so ignore the whole line
            //new_message.stream->printf("ok\n");

        } else if(!parse_command(cmd.c_str(), possible_command, new_message.stream)) {
            new_message.stream->printf("error:Unsupported command - %s\n", cmd.c_str());
        }
    }
}

// Act upon an ls command
// Convert the first parameter into an absolute path, then list the files in that path
void SimpleShell::ls_command( string parameters, StreamOutput *stream )
{
    string path, opts;
    while(!parameters.empty()) {
        string s = shift_parameter( parameters );
        if(s.front() == '-') {
            opts.append(s);
        } else {
            path = s;
            if(!parameters.empty()) {
                path.append(" ");
                path.append(parameters);
            }
            break;
        }
    }

    path = absolute_from_relative(path);

    DIR *d;
    struct dirent *p;
    struct tm timeinfo;
    char dirTmp[256]; 
    unsigned int npos=0;
    d = opendir(path.c_str());
    if (d != NULL) {
        while ((p = readdir(d)) != NULL) {
        	if (p->d_name[0] == '.') {
        		continue;
        	}
        	for (int i = 0; i < NAME_MAX; i ++) {
        		if (p->d_name[i] == ' ') p->d_name[i] = 0x01;
        	}
        	if (opts.find("-s", 0, 2) != string::npos) {
        	    get_fftime(p->d_date, p->d_time, &timeinfo);
        		// name size date
                memset(dirTmp, 0, sizeof(dirTmp));
                sprintf(dirTmp, "%s%s %d %04d%02d%02d%02d%02d%02d\r\n", string(p->d_name).c_str(),  p->d_isdir ? "/" : "",
                		p->d_isdir ? 0 : p->d_fsize, timeinfo.tm_year + 1980, timeinfo.tm_mon, timeinfo.tm_mday,
                				timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        	} else {
        		// only name
                memset(dirTmp, 0, sizeof(dirTmp));
                sprintf(dirTmp, "%s%s\r\n", string(p->d_name).c_str(), p->d_isdir ? "/" : "");
        	}
        	memcpy(&xbuff[npos], dirTmp, strlen(dirTmp));
        	npos += strlen(dirTmp);
        	if(npos >= 7900)
        	{
        		stream->puts((char *)xbuff, npos);
        		npos = 0;
        	}
        	
        }
        if( npos != 0)
        {
        	stream->puts((char *)xbuff, npos);
        }
        closedir(d);
        if(opts.find("-e", 0, 2) != string::npos) {
        	char eot = EOT;
            stream->puts(&eot, 1);
        }
    } else {
        if(opts.find("-e", 0, 2) != string::npos) {
            stream->_putc(CAN);
        }
        stream->printf("Could not open directory %s\r\n", path.c_str());
    }
}

extern SDFAT mounter;

void SimpleShell::remount_command( string parameters, StreamOutput *stream )
{
    mounter.remount();
    stream->printf("remounted\r\n");
}

// Delete a file
void SimpleShell::rm_command( string parameters, StreamOutput *stream )
{
	bool send_eof = false;
    string path = absolute_from_relative(shift_parameter( parameters ));
    string md5_path = change_to_md5_path(path);
    string lz_path = change_to_lz_path(path);
    if(!parameters.empty() && shift_parameter(parameters) == "-e") {
    	send_eof = true;
    }

    string toRemove = absolute_from_relative(path);
    int s = remove(toRemove.c_str());
    if (s != 0) {
        if(send_eof) {
            stream->_putc(CAN);
        }
    	stream->printf("Could not delete %s \r\n", toRemove.c_str());
    } else {
    	string str_md5 = absolute_from_relative(md5_path);
    	s = remove(str_md5.c_str());
/*
		if (s != 0) {
			if(send_eof) {
				stream->_putc(CAN);
			}
			stream->printf("Could not delete %s \r\n", str_md5.c_str());
		} 
		else {
			string str_lz = absolute_from_relative(lz_path);
			s = remove(str_lz.c_str());
			if (s != 0){
				if(send_eof) {
					stream->_putc(CAN);
				}
				stream->printf("Could not delete %s \r\n", str_lz.c_str());
			}
			else {
		        if(send_eof) {
		            stream->_putc(EOT);
	        	}
			
			}
    	}*/
    	string str_lz = absolute_from_relative(lz_path);
		s = remove(str_lz.c_str());
		if(send_eof) {
            stream->_putc(EOT);
    	}
    }
}

// Rename a file
void SimpleShell::mv_command( string parameters, StreamOutput *stream )
{
	bool send_eof = false;
    string from = absolute_from_relative(shift_parameter( parameters ));
    string md5_from = change_to_md5_path(from);
    string lz_from = change_to_lz_path(from);
    string to = absolute_from_relative(shift_parameter(parameters));
    string md5_to = change_to_md5_path(to);
    string lz_to = change_to_lz_path(to);
    if(!parameters.empty() && shift_parameter(parameters) == "-e") {
    	send_eof = true;
    }
    int s = rename(from.c_str(), to.c_str());
    if (s != 0)  {
    	if (send_eof) {
    		stream->_putc(CAN);
    	}
    	stream->printf("Could not rename %s to %s\r\n", from.c_str(), to.c_str());
    } else  {
    	s = rename(md5_from.c_str(), md5_to.c_str());
/*        if (s != 0)  {
        	if (send_eof) {
        		stream->_putc(CAN);
        	}
        	stream->printf("Could not rename %s to %s\r\n", md5_from.c_str(), md5_to.c_str());
        }
        else {
        	s = rename(lz_from.c_str(), lz_to.c_str());
        	if (s != 0)  {
	        	if (send_eof) {
	        		stream->_putc(CAN);
	        	}
	        	stream->printf("Could not rename %s to %s\r\n", lz_from.c_str(), lz_to.c_str());
        	}
        	else {
        		if (send_eof) {
				stream->_putc(EOT);
				}
				stream->printf("renamed %s to %s\r\n", from.c_str(), to.c_str());
        	}
        }*/
        s = rename(lz_from.c_str(), lz_to.c_str());
        if (send_eof) {
			stream->_putc(EOT);
		}
		stream->printf("renamed %s to %s\r\n", from.c_str(), to.c_str());
    }
}

// Create a new directory
void SimpleShell::mkdir_command( string parameters, StreamOutput *stream )
{
	bool send_eof = false;
    string path = absolute_from_relative(shift_parameter( parameters ));
    string md5_path = change_to_md5_path(path);
    string lz_path = change_to_lz_path(path);
    if(!parameters.empty() && shift_parameter(parameters) == "-e") {
    	send_eof = true;
    }
    int result = mkdir(path.c_str(), 0);
    if (result != 0) {
    	if (send_eof) {
    		stream->_putc(CAN); // ^Z terminates error
    	}
    	stream->printf("could not create directory %s\r\n", path.c_str());
    } else {
    	result = mkdir(md5_path.c_str(), 0);
/*        if (result != 0) {
        	if (send_eof) {
        		stream->_putc(CAN); // ^Z terminates error
        	}
        	stream->printf("could not create md5 directory %s\r\n", md5_path.c_str());
        } 
        else if (mkdir(lz_path.c_str(), 0) != 0) {
        	if (send_eof) {
        		stream->_putc(CAN); // ^Z terminates error
        	}
        	stream->printf("could not create lz directory %s\r\n", lz_path.c_str());
        }    
        else {
        	if (send_eof) {
            	stream->_putc(EOT); // ^D terminates the upload
        	}
        	stream->printf("created directory %s\r\n", path.c_str());
        }
*/
		mkdir(lz_path.c_str(), 0);
		if (send_eof) {
            	stream->_putc(EOT); // ^D terminates the upload
        	}
        stream->printf("created directory %s\r\n", path.c_str());
		
    }
}

// Change current absolute path to provided path
void SimpleShell::cd_command( string parameters, StreamOutput *stream )
{
    string folder = absolute_from_relative( parameters );

    DIR *d;
    d = opendir(folder.c_str());
    if (d == NULL) {
        stream->printf("Could not open directory %s \r\n", folder.c_str() );
    } else {
        THEKERNEL->current_path = folder;
        closedir(d);
    }
}

// Responds with the present working directory
void SimpleShell::pwd_command( string parameters, StreamOutput *stream )
{
    stream->printf("%s\r\n", THEKERNEL->current_path.c_str());
}

// Output the contents of a file, first parameter is the filename, second is the limit ( in number of lines to output )
void SimpleShell::cat_command( string parameters, StreamOutput *stream )
{
    // Get parameters ( filename and line limit )
    string filename = absolute_from_relative(shift_parameter(parameters));
    int limit = -1;
    int delay= 0;
    // parse parameters
    while (parameters != "") {
    	string s = shift_parameter(parameters);
        if ( s == "-d" ) {
            string d = shift_parameter(parameters);
            char *e = NULL;
            delay = strtol(d.c_str(), &e, 10);
            if (e <= d.c_str()) {
                delay = 0;

            }
        } else if (s != "") {
            char *e = NULL;
            limit = strtol(s.c_str(), &e, 10);
            if (e <= s.c_str())
                limit = -1;
        }
    }


    // we have been asked to delay before cat, probably to allow time to issue upload command
    if (delay > 0) {
        safe_delay_ms(delay * 1000);
    }

    // Open file
    FILE *lp = fopen(filename.c_str(), "r");
    if (lp == NULL) {
        stream->printf("File not found: %s\r\n", filename.c_str());
        return;
    }
    // string buffer;
    char buffer[192];
    memset(buffer, 0, sizeof(buffer));
    int c;
    int newlines = 0;
    int charcnt = 0;
    int sentcnt = 0;

    while ((c = fgetc (lp)) != EOF) {
    	buffer[charcnt] = c;
        if (c == '\n') newlines ++;
        // buffer.append((char *)&c, 1);
        charcnt ++;
        if (charcnt > 190) {
            sentcnt = stream->puts(buffer);
            // if (sentcnt < strlen()(int)buffer.size()) {
            if (sentcnt < (int)strlen(buffer)) {
            	fclose(lp);
            	stream->printf("Caching error, line: %d, size: %d, sent: %d", newlines, strlen(buffer), sentcnt);
            	return;
            }
            // buffer.clear();
            memset(buffer, 0, sizeof(buffer));
            charcnt = 0;
            // we need to kick things or they die
            THEKERNEL->call_event(ON_IDLE);
        }
        if ( newlines == limit ) {
            break;
        }
    };
    fclose(lp);
    lp = NULL;

    // send last line
    // if (buffer.size() > 0) {
    if (strlen(buffer) > 0) {
    	// stream->puts(buffer.c_str());
    	stream->puts(buffer);
    }
}

// echo commands
void SimpleShell::echo_command( string parameters, StreamOutput *stream )
{
    //send to all streams
    printk("echo: %s\r\n", parameters.c_str());
}

// loads the specified config-override file
void SimpleShell::load_command( string parameters, StreamOutput *stream )
{
    // Get parameters ( filename )
    string filename = absolute_from_relative(parameters);
    if(filename == "/") {
        filename = THEKERNEL->config_override_filename();
    }

    FILE *fp = fopen(filename.c_str(), "r");
    if(fp != NULL) {
        char buf[132];
        stream->printf("Loading config override file: %s...\n", filename.c_str());
        while(fgets(buf, sizeof buf, fp) != NULL) {
            stream->printf("  %s", buf);
            if(buf[0] == ';') continue; // skip the comments
            // NOTE only Gcodes and Mcodes can be in the config-override
            Gcode *gcode = new Gcode(buf, &StreamOutput::NullStream);
            THEKERNEL->call_event(ON_GCODE_RECEIVED, gcode);
            delete gcode;
            THEKERNEL->call_event(ON_IDLE);
        }
        stream->printf("config override file executed\n");
        fclose(fp);

    } else {
        stream->printf("File not found: %s\n", filename.c_str());
    }
}

// saves the specified config-override file
void SimpleShell::save_command( string parameters, StreamOutput *stream )
{
    // Get parameters ( filename )
    string filename = absolute_from_relative(parameters);
    if(filename == "/") {
        filename = THEKERNEL->config_override_filename();
    }

    THECONVEYOR->wait_for_idle(); //just to be safe as it can take a while to run

    //remove(filename.c_str()); // seems to cause a hang every now and then
    {
        FileStream fs(filename.c_str());
        fs.printf("; DO NOT EDIT THIS FILE\n");
        // this also will truncate the existing file instead of deleting it
    }

    // stream that appends to file
    AppendFileStream *gs = new AppendFileStream(filename.c_str());
    // if(!gs->is_open()) {
    //     stream->printf("Unable to open File %s for write\n", filename.c_str());
    //     return;
    // }

    __disable_irq();
    // issue a M500 which will store values in the file stream
    Gcode *gcode = new Gcode("M500", gs);
    THEKERNEL->call_event(ON_GCODE_RECEIVED, gcode );
    delete gs;
    delete gcode;
    __enable_irq();

    stream->printf("Settings Stored to %s\r\n", filename.c_str());
}

// show free memory
void SimpleShell::mem_command( string parameters, StreamOutput *stream)
{
    bool verbose = shift_parameter( parameters ).find_first_of("Vv") != string::npos;
    unsigned long heap = (unsigned long)_sbrk(0);
    unsigned long m = g_maximumHeapAddress - heap;
    stream->printf("Unused Heap: %lu bytes\r\n", m);

    uint32_t f = heapWalk(stream, verbose);
    stream->printf("Total Free RAM: %lu bytes\r\n", m + f);

    stream->printf("Free AHB0: %lu, AHB1: %lu\r\n", AHB0.free(), AHB1.free());
    if (verbose) {
        AHB0.debug(stream);
        AHB1.debug(stream);
    }

    stream->printf("Block size: %u bytes, Tickinfo size: %u bytes\n", sizeof(Block), sizeof(Block::tickinfo_t) * Block::n_actuators);
}

static uint32_t getDeviceType()
{
#define IAP_LOCATION 0x1FFF1FF1
    uint32_t command[1];
    uint32_t result[5];
    typedef void (*IAP)(uint32_t *, uint32_t *);
    IAP iap = (IAP) IAP_LOCATION;

    __disable_irq();

    command[0] = 54;
    iap(command, result);

    __enable_irq();

    return result[1];
}


// get network config
void SimpleShell::time_command( string parameters, StreamOutput *stream)
{
    if (!parameters.empty() ) {
    	time_t new_time = strtol(parameters.c_str(), NULL, 10);
    	set_time(new_time);
    } else {
    	time_t old_time = time(NULL);
    	stream->printf("time = %ld\n", old_time);
    }
}



// get network config
void SimpleShell::net_command( string parameters, StreamOutput *stream)
{
	/*
    void *returned_data;
    bool ok = PublicData::get_value( network_checksum, get_ipconfig_checksum, &returned_data );
    if(ok) {
        char *str = (char *)returned_data;
        stream->printf("%s\r\n", str);
        free(str);

    } else {
        stream->printf("No network detected\n");
    }*/
}

// get or set ap channel config
void SimpleShell::ap_command( string parameters, StreamOutput *stream)
{
	uint8_t channel;
	char buff[32];
	memset(buff, 0, sizeof(buff));
    if (!parameters.empty() ) {
    	string s = shift_parameter( parameters );
    	if (s == "channel") {
    		if (!parameters.empty()) {
    			channel = strtol(parameters.c_str(), NULL, 10);
    	    	if (channel < 1 || channel > 14) {
    	    		stream->printf("WiFi AP Channel should between 1 to 14\n");
    	    	} else {
    	            PublicData::set_value( wlan_checksum, ap_set_channel_checksum, &channel );
    	    	}
    		}
    	} else if (s == "ssid") {
    		if (!parameters.empty()) {
    	    	if (parameters.length() > 27) {
    	    		stream->printf("WiFi AP SSID length should between 1 to 27\n");
    	    	} else {
    	    		strcpy(buff, parameters.c_str());
    	            PublicData::set_value( wlan_checksum, ap_set_ssid_checksum, buff );
    	    	}
    		}
    	} else if (s == "password") {
    		if (!parameters.empty()) {
    	    	if (parameters.length() < 8) {
    	    		stream->printf("WiFi AP password length should more than 7\n");
    	    		return;
    	    	} else {
    	    		strcpy(buff, parameters.c_str());
    	    	}
    		}
	        PublicData::set_value( wlan_checksum, ap_set_password_checksum, buff );
    	} else if (s == "enable") {
    		bool b = true;
	        PublicData::set_value( wlan_checksum, ap_enable_checksum, &b );
    	} else if (s == "disable") {
    		bool b = false;
	        PublicData::set_value( wlan_checksum, ap_enable_checksum, &b );
    	} else {
    		stream->printf("ERROR: Invalid AP Command!\n");
    	}
    }
}


// wlan config
void SimpleShell::wlan_command( string parameters, StreamOutput *stream)
{
	bool send_eof = false;
	bool disconnect = false;
    string ssid, password;

    while (!parameters.empty()) {
        string s = shift_parameter( parameters );
        if(s == "-e") {
        	send_eof = true;
        } else if (s == "-d") {
        	disconnect = true;
        } else {
        	if (ssid.empty()) {
            	ssid = s;
            } else if (password.empty()) {
            	password = s;
            }
        }
    }

    void *returned_data;
    if (ssid.empty()) {
    	if (!send_eof)
    		stream->printf("Scanning wifi signals...\n");
        bool ok = PublicData::get_value( wlan_checksum, get_wlan_checksum, &returned_data );
        if (ok) {
            char *str = (char *)returned_data;
            stream->printf("%s", str);
            free(str);
        	if (send_eof) {
            	stream->_putc(EOT);
        	}

        } else {
        	if (send_eof) {
        		stream->_putc(CAN);
        	} else {
                stream->printf("No wlan detected\n");
        	}
        }
    } else {
    	if (!send_eof) {
    		if (disconnect) {
    			stream->printf("Disconnecting from wifi...\n");
    		} else {
    			stream->printf("Connecting to wifi: %s...\n", ssid.c_str());
    		}
    	}
    	ap_conn_info t;
    	t.disconnect = disconnect;
    	if (!t.disconnect) {
        	snprintf(t.ssid, sizeof(t.ssid), "%s", ssid.c_str());
        	snprintf(t.password, sizeof(t.password), "%s", password.c_str());
    	}
        bool ok = PublicData::set_value( wlan_checksum, set_wlan_checksum, &t );
        if (ok) {
        	if (t.has_error) {
                stream->printf("Error: %s\n", t.error_info);
            	if (send_eof) {
            		stream->_putc(CAN);
            	}
        	} else {
        		if (t.disconnect) {
            		stream->printf("Wifi Disconnected!\n");
        		} else {
            		stream->printf("Wifi connected, ip: %s\n", t.ip_address);
        		}
            	if (send_eof) {
                	stream->_putc(EOT);
            	}
        	}
        } else {
            stream->printf("%s\n", "Parameter error when setting wlan!");
        	if (send_eof) {
        		stream->_putc(CAN);
        	}
        }
    }
}

// wlan config
void SimpleShell::diagnose_command( string parameters, StreamOutput *stream)
{
	std::string str;
    size_t n;
    char buf[128];
    bool ok = false;

    str.append("{");

    // get spindle state
    struct spindle_status ss;
    ok = PublicData::get_value(pwm_spindle_control_checksum, get_spindle_status_checksum, &ss);
    if (ok) {
        n = snprintf(buf, sizeof(buf), "S:%d,%d", (int)ss.state, (int)ss.target_rpm);
        if(n > sizeof(buf)) n= sizeof(buf);
        str.append(buf, n);
    }

    // get laser state
    struct laser_status ls;
    ok = PublicData::get_value(laser_checksum, get_laser_status_checksum, &ls);
    if (ok) {
        n = snprintf(buf, sizeof(buf), "|L:%d,%d", (int)ls.state, (int)ls.power);
        if(n > sizeof(buf)) n= sizeof(buf);
        str.append(buf, n);
    }

    // get switchs state
    struct pad_switch pad;
    ok = PublicData::get_value(switch_checksum, get_checksum("vacuum"), 0, &pad);
    if (ok) {
        n = snprintf(buf, sizeof(buf), "|V:%d,%d", (int)pad.state, (int)pad.value);
        if(n > sizeof(buf)) n = sizeof(buf);
        str.append(buf, n);
    }
    ok = PublicData::get_value(switch_checksum, get_checksum("spindlefan"), 0, &pad);
    if (ok) {
        n = snprintf(buf, sizeof(buf), "|F:%d,%d", (int)pad.state, (int)pad.value);
        if(n > sizeof(buf)) n = sizeof(buf);
        str.append(buf, n);
    }
    ok = PublicData::get_value(switch_checksum, get_checksum("light"), 0, &pad);
    if (ok) {
        n = snprintf(buf, sizeof(buf), "|G:%d", (int)pad.state);
        if(n > sizeof(buf)) n = sizeof(buf);
        str.append(buf, n);
    }
    ok = PublicData::get_value(switch_checksum, get_checksum("toolsensor"), 0, &pad);
    if (ok) {
        n = snprintf(buf, sizeof(buf), "|T:%d", (int)pad.state);
        if(n > sizeof(buf)) n = sizeof(buf);
        str.append(buf, n);
    }
    ok = PublicData::get_value(switch_checksum, get_checksum("air"), 0, &pad);
    if (ok) {
        n = snprintf(buf, sizeof(buf), "|R:%d", (int)pad.state);
        if(n > sizeof(buf)) n = sizeof(buf);
        str.append(buf, n);
    }
    ok = PublicData::get_value(switch_checksum, get_checksum("probecharger"), 0, &pad);
    if (ok) {
        n = snprintf(buf, sizeof(buf), "|C:%d", (int)pad.state);
        if(n > sizeof(buf)) n = sizeof(buf);
        str.append(buf, n);
    }


    // get states
    char data[11];
    ok = PublicData::get_value(endstops_checksum, get_endstop_states_checksum, 0, data);
    if (ok) {
        n = snprintf(buf, sizeof(buf), "|E:%d,%d,%d,%d,%d,%d", data[0], data[1], data[2], data[3], data[4], data[5]);
        if(n > sizeof(buf)) n = sizeof(buf);
        str.append(buf, n);
    }

    // get probe and calibrate states
    ok = PublicData::get_value(zprobe_checksum, get_zprobe_pin_states_checksum, 0, &data[6]);
    if (ok) {
        n = snprintf(buf, sizeof(buf), "|P:%d,%d", data[6], data[7]);
        if(n > sizeof(buf)) n = sizeof(buf);
        str.append(buf, n);
    }

    // get atc endstop and tool senser states
    ok = PublicData::get_value(atc_handler_checksum, get_atc_pin_status_checksum, 0, &data[8]);
    if (ok) {
        n = snprintf(buf, sizeof(buf), "|A:%d,%d", data[8], data[9]);
        if(n > sizeof(buf)) n = sizeof(buf);
        str.append(buf, n);
    }

    // get e-stop states
    ok = PublicData::get_value(main_button_checksum, get_e_stop_state_checksum, 0, &data[10]);
    if (ok) {
        n = snprintf(buf, sizeof(buf), "|I:%d", data[10]);
        if(n > sizeof(buf)) n = sizeof(buf);
        str.append(buf, n);
    }


    str.append("}\n");
    stream->printf("%s", str.c_str());

}

// sleep command
void SimpleShell::sleep_command(string parameters, StreamOutput *stream)
{
	char power_off = 0;
	// turn off 12V/24V power supply
	PublicData::set_value( main_button_checksum, switch_power_12_checksum, &power_off );
	PublicData::set_value( main_button_checksum, switch_power_24_checksum, &power_off );
	THEKERNEL->set_sleeping(true);
	THEKERNEL->call_event(ON_HALT, nullptr);
}

// sleep command
void SimpleShell::power_command(string parameters, StreamOutput *stream)
{
	char power_on = 1;
	char power_off = 0;
	if (!parameters.empty()) {
		string s1 = shift_parameter( parameters );
		string s2 = "";
		if (!parameters.empty()) {
			s2 = shift_parameter( parameters );
		}
		if (s1 == "on" ) {
			if (s2 == "12") {
				PublicData::set_value( main_button_checksum, switch_power_12_checksum, &power_on );
			} else if (s2 == "24") {
				PublicData::set_value( main_button_checksum, switch_power_24_checksum, &power_on );
			}
		} else if (s1 == "off") {
			if (s2 == "12") {
				PublicData::set_value( main_button_checksum, switch_power_12_checksum, &power_off );
			} else if (s2 == "24") {
				PublicData::set_value( main_button_checksum, switch_power_24_checksum, &power_off );
			}
		}
	}
}

// Print the types of files we support for uploading
void SimpleShell::ftype_command( string parameters, StreamOutput *stream )
{
	stream->printf("ftype = %s\n", FILETYPE);
}
// print out build version
void SimpleShell::version_command( string parameters, StreamOutput *stream )
{
	stream->printf("version = %s\n", VERSION);
}

// Reset the system
void SimpleShell::reset_command( string parameters, StreamOutput *stream)
{
    stream->printf("Rebooting machine in 3 seconds...\r\n");
    reset_delay_secs = 3; // reboot in 3 seconds
}

// go into dfu boot mode
void SimpleShell::dfu_command( string parameters, StreamOutput *stream)
{
    stream->printf("Entering boot mode...\r\n");
    system_reset(true);
}

// Break out into the MRI debugging system
void SimpleShell::break_command( string parameters, StreamOutput *stream)
{
    stream->printf("Entering MRI debug mode...\r\n");
    __debugbreak();
}

static int get_active_tool()
{
    void *returned_data;
    bool ok = PublicData::get_value(tool_manager_checksum, get_active_tool_checksum, &returned_data);
    if (ok) {
         int active_tool=  *static_cast<int *>(returned_data);
        return active_tool;
    } else {
        return 0;
    }
}

static bool get_switch_state(const char *sw)
{
    // get sw switch state
    struct pad_switch pad;
    bool ok = PublicData::get_value(switch_checksum, get_checksum(sw), 0, &pad);
    if (!ok) {
        return false;
    }
    return pad.state;
}

void SimpleShell::grblDP_command( string parameters, StreamOutput *stream)
{
    /*
    [G54:95.000,40.000,-23.600]
    [G55:0.000,0.000,0.000]
    [G56:0.000,0.000,0.000]
    [G57:0.000,0.000,0.000]
    [G58:0.000,0.000,0.000]
    [G59:0.000,0.000,0.000]
    [G28:0.000,0.000,0.000]
    [G30:0.000,0.000,0.000]
    [G92:0.000,0.000,0.000]
    [TLO:0.000]
    [PRB:0.000,0.000,0.000:0]
    */

    bool verbose = shift_parameter( parameters ).find_first_of("Vv") != string::npos;

    std::vector<Robot::wcs_t> v= THEROBOT->get_wcs_state();
    if(verbose) {
        char current_wcs= std::get<0>(v[0]);
        stream->printf("[current WCS: %s]\n", wcs2gcode(current_wcs).c_str());
    }

    int n= std::get<1>(v[0]);
    for (int i = 1; i <= n; ++i) {
        stream->printf("[%s:%1.4f,%1.4f,%1.4f]\n", wcs2gcode(i-1).c_str(),
            THEROBOT->from_millimeters(std::get<0>(v[i])),
            THEROBOT->from_millimeters(std::get<1>(v[i])),
            THEROBOT->from_millimeters(std::get<2>(v[i])));
    }

    float *rd;
    PublicData::get_value( endstops_checksum, g28_position_checksum, &rd );
    stream->printf("[G28:%1.4f,%1.4f,%1.4f]\n",
        THEROBOT->from_millimeters(rd[0]),
        THEROBOT->from_millimeters(rd[1]),
        THEROBOT->from_millimeters(rd[2]));

    stream->printf("[G30:%1.4f,%1.4f,%1.4f]\n", 0.0, 0.0, 0.0); // not supported

    stream->printf("[G92:%1.4f,%1.4f,%1.4f]\n",
        THEROBOT->from_millimeters(std::get<0>(v[n+1])),
        THEROBOT->from_millimeters(std::get<1>(v[n+1])),
        THEROBOT->from_millimeters(std::get<2>(v[n+1])));

    if(verbose) {
        stream->printf("[Tool Offset:%1.4f,%1.4f,%1.4f]\n",
            THEROBOT->from_millimeters(std::get<0>(v[n+2])),
            THEROBOT->from_millimeters(std::get<1>(v[n+2])),
            THEROBOT->from_millimeters(std::get<2>(v[n+2])));
    }else{
        stream->printf("[TL0:%1.4f]\n", THEROBOT->from_millimeters(std::get<2>(v[n+2])));
    }

    // this is the last probe position, updated when a probe completes, also stores the number of steps moved after a homing cycle
    float px, py, pz;
    uint8_t ps;
    std::tie(px, py, pz, ps) = THEROBOT->get_last_probe_position();
    stream->printf("[PRB:%1.4f,%1.4f,%1.4f:%d]\n", THEROBOT->from_millimeters(px), THEROBOT->from_millimeters(py), THEROBOT->from_millimeters(pz), ps);
}

void SimpleShell::get_command( string parameters, StreamOutput *stream)
{
    string what = shift_parameter( parameters );

    if (what == "temp") {
        struct pad_temperature temp;
        string type = shift_parameter( parameters );
        if(type.empty()) {
            // scan all temperature controls
            std::vector<struct pad_temperature> controllers;
            bool ok = PublicData::get_value(temperature_control_checksum, poll_controls_checksum, &controllers);
            if (ok) {
                for (auto &c : controllers) {
                   stream->printf("%s (%d) temp: %f/%f @%d\r\n", c.designator.c_str(), c.id, c.current_temperature, c.target_temperature, c.pwm);
                }

            } else {
                stream->printf("no heaters found\r\n");
            }

        }else{
            bool ok = PublicData::get_value( temperature_control_checksum, current_temperature_checksum, get_checksum(type), &temp );

            if (ok) {
                stream->printf("%s temp: %f/%f @%d\r\n", type.c_str(), temp.current_temperature, temp.target_temperature, temp.pwm);
            } else {
                stream->printf("%s is not a known temperature device\r\n", type.c_str());
            }
        }

    } else if (what == "fk" || what == "ik") {
        string p= shift_parameter( parameters );
        bool move= false;
        if(p == "-m") {
            move= true;
            p= shift_parameter( parameters );
        }

        std::vector<float> v= parse_number_list(p.c_str());
        if(p.empty() || v.size() < 1) {
            stream->printf("error:usage: get [fk|ik] [-m] x[,y,z]\n");
            return;
        }

        float x= v[0];
        float y= (v.size() > 1) ? v[1] : x;
        float z= (v.size() > 2) ? v[2] : y;

        if(what == "fk") {
            // do forward kinematics on the given actuator position and display the cartesian coordinates
            ActuatorCoordinates apos{x, y, z};
            float pos[3];
            THEROBOT->arm_solution->actuator_to_cartesian(apos, pos);
            stream->printf("cartesian= X %f, Y %f, Z %f\n", pos[0], pos[1], pos[2]);
            x= pos[0];
            y= pos[1];
            z= pos[2];

        }else{
            // do inverse kinematics on the given cartesian position and display the actuator coordinates
            float pos[3]{x, y, z};
            ActuatorCoordinates apos;
            THEROBOT->arm_solution->cartesian_to_actuator(pos, apos);
            stream->printf("actuator= X %f, Y %f, Z %f\n", apos[0], apos[1], apos[2]);
        }

        if(move) {
            // move to the calculated, or given, XYZ
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "G53 G0 X%f Y%f Z%f", THEROBOT->from_millimeters(x), THEROBOT->from_millimeters(y), THEROBOT->from_millimeters(z));
            struct SerialMessage message;
            message.message = cmd;
            message.stream = &(StreamOutput::NullStream);
            message.line = 0;
            THEKERNEL->call_event(ON_CONSOLE_LINE_RECEIVED, &message );
            THECONVEYOR->wait_for_idle();
        }

   } else if (what == "pos") {
        // convenience to call all the various M114 variants, shows ABC axis where relevant
        std::string buf;
        THEROBOT->print_position(0, buf); stream->printf("last %s\n", buf.c_str()); buf.clear();
        THEROBOT->print_position(1, buf); stream->printf("realtime %s\n", buf.c_str()); buf.clear();
        THEROBOT->print_position(2, buf); stream->printf("%s\n", buf.c_str()); buf.clear();
        THEROBOT->print_position(3, buf); stream->printf("%s\n", buf.c_str()); buf.clear();
        THEROBOT->print_position(4, buf); stream->printf("%s\n", buf.c_str()); buf.clear();
        THEROBOT->print_position(5, buf); stream->printf("%s\n", buf.c_str()); buf.clear();

    } else if (what == "wcs") {
        // print the wcs state
        grblDP_command("-v", stream);

    } else if (what == "state") {
        // also $G and $I
        // [G0 G54 G17 G21 G90 G94 M0 M5 M9 T0 F0.]
        stream->printf("[G%d %s G%d G%d G%d G94 M0 M%c M%c T%d F%1.4f S%1.4f]\n",
            THEKERNEL->gcode_dispatch->get_modal_command(),
            wcs2gcode(THEROBOT->get_current_wcs()).c_str(),
            THEROBOT->plane_axis_0 == X_AXIS && THEROBOT->plane_axis_1 == Y_AXIS && THEROBOT->plane_axis_2 == Z_AXIS ? 17 :
              THEROBOT->plane_axis_0 == X_AXIS && THEROBOT->plane_axis_1 == Z_AXIS && THEROBOT->plane_axis_2 == Y_AXIS ? 18 :
              THEROBOT->plane_axis_0 == Y_AXIS && THEROBOT->plane_axis_1 == Z_AXIS && THEROBOT->plane_axis_2 == X_AXIS ? 19 : 17,
            THEROBOT->inch_mode ? 20 : 21,
            THEROBOT->absolute_mode ? 90 : 91,
            get_switch_state("spindle") ? '3' : '5',
            get_switch_state("mist") ? '7' : get_switch_state("flood") ? '8' : '9',
            get_active_tool(),
            THEROBOT->from_millimeters(THEROBOT->get_feed_rate()),
            THEROBOT->get_s_value());

    } else if (what == "status") {
        // also ? on serial and usb
        stream->printf("%s\n", THEKERNEL->get_query_string().c_str());

    } else if (what == "compensation") {
    	float mpos[3];
    	THEROBOT->get_current_machine_position(mpos);
    	float old_mpos[3];
    	memcpy(old_mpos, mpos, sizeof(mpos));
		// current_position/mpos includes the compensation transform so we need to get the inverse to get actual position
		if(THEROBOT->compensationTransform) THEROBOT->compensationTransform(mpos, true, true); // get inverse compensation transform
		stream->printf("Curr: %1.3f,%1.3f,%1.3f, Comp: %1.3f,%1.3f,%1.3f\n", old_mpos[0], old_mpos[1], old_mpos[2], mpos[0], mpos[1], mpos[2]);
    } else if (what == "wp" || what == "wp_state") {
    	PublicData::get_value(atc_handler_checksum, show_wp_state_checksum, NULL);
    } else {
        stream->printf("error: unknown option %s\n", what.c_str());
    }
}

// used to test out the get public data events
void SimpleShell::set_temp_command( string parameters, StreamOutput *stream)
{
    string type = shift_parameter( parameters );
    string temp = shift_parameter( parameters );
    float t = temp.empty() ? 0.0 : strtof(temp.c_str(), NULL);
    bool ok = PublicData::set_value( temperature_control_checksum, get_checksum(type), &t );

    if (ok) {
        stream->printf("%s temp set to: %3.1f\r\n", type.c_str(), t);
    } else {
        stream->printf("%s is not a known temperature device\r\n", type.c_str());
    }
}

void SimpleShell::print_thermistors_command( string parameters, StreamOutput *stream)
{
    // #ifndef NO_TOOLS_TEMPERATURECONTROL
    Thermistor::print_predefined_thermistors(stream);
    // #endif
}

void SimpleShell::calc_thermistor_command( string parameters, StreamOutput *stream)
{
    // #ifndef NO_TOOLS_TEMPERATURECONTROL
    string s = shift_parameter( parameters );
    int saveto= -1;
    // see if we have -sn as first argument
    if(s.find("-s", 0, 2) != string::npos) {
        // save the results to thermistor n
        saveto= strtol(s.substr(2).c_str(), nullptr, 10);
    }else{
        parameters= s;
    }

    std::vector<float> trl= parse_number_list(parameters.c_str());
    if(trl.size() == 6) {
        // calculate the coefficients
        float c1, c2, c3;
        std::tie(c1, c2, c3) = Thermistor::calculate_steinhart_hart_coefficients(trl[0], trl[1], trl[2], trl[3], trl[4], trl[5]);
        stream->printf("Steinhart Hart coefficients:  I%1.18f J%1.18f K%1.18f\n", c1, c2, c3);
        if(saveto == -1) {
            stream->printf("  Paste the above in the M305 S0 command, then save with M500\n");
        }else{
            char buf[80];
            size_t n = snprintf(buf, sizeof(buf), "M305 S%d I%1.18f J%1.18f K%1.18f", saveto, c1, c2, c3);
            if(n > sizeof(buf)) n= sizeof(buf);
            string g(buf, n);
            Gcode gcode(g, &(StreamOutput::NullStream));
            THEKERNEL->call_event(ON_GCODE_RECEIVED, &gcode );
            stream->printf("  Setting Thermistor %d to those settings, save with M500\n", saveto);
        }

    }else{
        // give help
        stream->printf("Usage: calc_thermistor T1,R1,T2,R2,T3,R3\n");
    }
    // #endif
}

// set or get switch state for a named switch
void SimpleShell::switch_command( string parameters, StreamOutput *stream)
{
    string type;
    string value;

    if(parameters[0] == '$') {
        // $S command
        type = shift_parameter( parameters );
        while(!type.empty()) {
            struct pad_switch pad;
            bool ok = PublicData::get_value(switch_checksum, get_checksum(type), 0, &pad);
            if(ok) {
                stream->printf("switch %s is %d\n", type.c_str(), pad.state);
            }

            type = shift_parameter( parameters );
        }
        return;

    }else{
        type = shift_parameter( parameters );
        value = shift_parameter( parameters );
    }

    bool ok = false;
    if(value.empty()) {
        // get switch state
        struct pad_switch pad;
        bool ok = PublicData::get_value(switch_checksum, get_checksum(type), 0, &pad);
        if (!ok) {
            stream->printf("unknown switch %s.\n", type.c_str());
            return;
        }
        stream->printf("switch %s is %d\n", type.c_str(), pad.state);

    }else{
        // set switch state
        if(value == "on" || value == "off") {
            bool b = value == "on";
            ok = PublicData::set_value( switch_checksum, get_checksum(type), state_checksum, &b );
        } else {
            stream->printf("must be either on or off\n");
            return;
        }
        if (ok) {
            stream->printf("switch %s set to: %s\n", type.c_str(), value.c_str());
        } else {
            stream->printf("%s is not a known switch device\n", type.c_str());
        }
    }
}

void SimpleShell::md5sum_command( string parameters, StreamOutput *stream )
{
	string filename = absolute_from_relative(parameters);

	// Open file
	FILE *lp = fopen(filename.c_str(), "r");
	if (lp == NULL) {
		stream->printf("File not found: %s\r\n", filename.c_str());
		return;
	}
	MD5 md5;
	uint8_t buf[64];
	do {
		size_t n= fread(buf, 1, sizeof buf, lp);
		if(n > 0) md5.update(buf, n);
		THEKERNEL->call_event(ON_IDLE);
	} while(!feof(lp));

	stream->printf("%s %s\n", md5.finalize().hexdigest().c_str(), filename.c_str());
	fclose(lp);

}

// runs several types of test on the mechanisms
void SimpleShell::test_command( string parameters, StreamOutput *stream)
{
    AutoPushPop app; // this will save the state and restore it on exit
    string what = shift_parameter( parameters );

    if (what == "jog") {
        // jogs back and forth usage: axis distance iterations [feedrate]
        string axis = shift_parameter( parameters );
        string dist = shift_parameter( parameters );
        string iters = shift_parameter( parameters );
        string speed = shift_parameter( parameters );
        if(axis.empty() || dist.empty() || iters.empty()) {
            stream->printf("error: Need axis distance iterations\n");
            return;
        }
        float d= strtof(dist.c_str(), NULL);
        float f= speed.empty() ? THEROBOT->get_feed_rate() : strtof(speed.c_str(), NULL);
        uint32_t n= strtol(iters.c_str(), NULL, 10);

        bool toggle= false;
        for (uint32_t i = 0; i < n; ++i) {
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "G91 G0 %c%f F%f G90", toupper(axis[0]), toggle ? -d : d, f);
            stream->printf("%s\n", cmd);
            struct SerialMessage message{&StreamOutput::NullStream, cmd, 0};
            THEKERNEL->call_event(ON_CONSOLE_LINE_RECEIVED, &message );
            if(THEKERNEL->is_halted()) break;
            toggle= !toggle;
        }
        stream->printf("done\n");

    }else if (what == "circle") {
        // draws a circle around origin. usage: radius iterations [feedrate]
        string radius = shift_parameter( parameters );
        string iters = shift_parameter( parameters );
        string speed = shift_parameter( parameters );
         if(radius.empty() || iters.empty()) {
            stream->printf("error: Need radius iterations\n");
            return;
        }

        float r= strtof(radius.c_str(), NULL);
        uint32_t n= strtol(iters.c_str(), NULL, 10);
        float f= speed.empty() ? THEROBOT->get_feed_rate() : strtof(speed.c_str(), NULL);

        THEROBOT->push_state();
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "G91 G0 X%f F%f G90", -r, f);
        stream->printf("%s\n", cmd);
        struct SerialMessage message{&StreamOutput::NullStream, cmd, 0};
        THEKERNEL->call_event(ON_CONSOLE_LINE_RECEIVED, &message );

        for (uint32_t i = 0; i < n; ++i) {
            if(THEKERNEL->is_halted()) break;
            snprintf(cmd, sizeof(cmd), "G2 I%f J0 F%f", r, f);
            stream->printf("%s\n", cmd);
            message.message= cmd;
            message.line = 0;
            THEKERNEL->call_event(ON_CONSOLE_LINE_RECEIVED, &message );
        }

        // leave it where it started
        if(!THEKERNEL->is_halted()) {
            snprintf(cmd, sizeof(cmd), "G91 G0 X%f F%f G90", r, f);
            stream->printf("%s\n", cmd);
            struct SerialMessage message{&StreamOutput::NullStream, cmd, 0};
            THEKERNEL->call_event(ON_CONSOLE_LINE_RECEIVED, &message );
        }

        THEROBOT->pop_state();
        stream->printf("done\n");

    }else if (what == "square") {
        // draws a square usage: size iterations [feedrate]
        string size = shift_parameter( parameters );
        string iters = shift_parameter( parameters );
        string speed = shift_parameter( parameters );
        if(size.empty() || iters.empty()) {
            stream->printf("error: Need size iterations\n");
            return;
        }
        float d= strtof(size.c_str(), NULL);
        float f= speed.empty() ? THEROBOT->get_feed_rate() : strtof(speed.c_str(), NULL);
        uint32_t n= strtol(iters.c_str(), NULL, 10);

        for (uint32_t i = 0; i < n; ++i) {
            char cmd[64];
            {
                snprintf(cmd, sizeof(cmd), "G91 G0 X%f F%f", d, f);
                stream->printf("%s\n", cmd);
                struct SerialMessage message{&StreamOutput::NullStream, cmd, 0};
                THEKERNEL->call_event(ON_CONSOLE_LINE_RECEIVED, &message );
            }
            {
                snprintf(cmd, sizeof(cmd), "G0 Y%f", d);
                stream->printf("%s\n", cmd);
                struct SerialMessage message{&StreamOutput::NullStream, cmd, 0};
                THEKERNEL->call_event(ON_CONSOLE_LINE_RECEIVED, &message );
            }
            {
                snprintf(cmd, sizeof(cmd), "G0 X%f", -d);
                stream->printf("%s\n", cmd);
                struct SerialMessage message{&StreamOutput::NullStream, cmd, 0};
                THEKERNEL->call_event(ON_CONSOLE_LINE_RECEIVED, &message );
            }
            {
                snprintf(cmd, sizeof(cmd), "G0 Y%f G90", -d);
                stream->printf("%s\n", cmd);
                struct SerialMessage message{&StreamOutput::NullStream, cmd, 0};
                THEKERNEL->call_event(ON_CONSOLE_LINE_RECEIVED, &message );
            }
            if(THEKERNEL->is_halted()) break;
         }
        stream->printf("done\n");

    }else if (what == "raw") {
        // issues raw steps to the specified axis usage: axis steps steps/sec
        string axis = shift_parameter( parameters );
        string stepstr = shift_parameter( parameters );
        string stepspersec = shift_parameter( parameters );
        if(axis.empty() || stepstr.empty() || stepspersec.empty()) {
            stream->printf("error: Need axis steps steps/sec\n");
            return;
        }

        char ax= toupper(axis[0]);
        uint8_t a= ax >= 'X' ? ax - 'X' : ax - 'A' + 3;
        int steps= strtol(stepstr.c_str(), NULL, 10);
        bool dir= steps >= 0;
        steps= std::abs(steps);

        if(a > C_AXIS) {
            stream->printf("error: axis must be x, y, z, a, b, c\n");
            return;
        }

        if(a >= THEROBOT->get_number_registered_motors()) {
            stream->printf("error: axis is out of range\n");
            return;
        }

        uint32_t sps= strtol(stepspersec.c_str(), NULL, 10);
        sps= std::max(sps, 1UL);

        uint32_t delayus= 1000000.0F / sps;
        for(int s= 0;s<steps;s++) {
            if(THEKERNEL->is_halted()) break;
            THEROBOT->actuators[a]->manual_step(dir);
            // delay but call on_idle
            safe_delay_us(delayus);
        }

        // reset the position based on current actuator position
        THEROBOT->reset_position_from_current_actuator_position();

        //stream->printf("done\n");

    }else {
        stream->printf("usage:\n test jog axis distance iterations [feedrate]\n");
        stream->printf(" test square size iterations [feedrate]\n");
        stream->printf(" test circle radius iterations [feedrate]\n");
        stream->printf(" test raw axis steps steps/sec\n");
    }
}

void SimpleShell::jog(string parameters, StreamOutput *stream)
{
    // $J X0.1 [Y0.2] [F0.5]
    int n_motors= THEROBOT->get_number_registered_motors();

    // get axis to move and amount (X0.1)
    // may specify multiple axis

    float rate_mm_s= NAN;
    float scale= 1.0F;
    float delta[n_motors];
    for (int i = 0; i < n_motors; ++i) {
        delta[i]= 0;
    }

    // $J is first parameter
    shift_parameter(parameters);
    if(parameters.empty()) {
        stream->printf("usage: $J X0.01 [F0.5] - axis can be XYZABC, optional speed is scale of max_rate\n");
        return;
    }

    while(!parameters.empty()) {
        string p= shift_parameter(parameters);

        char ax= toupper(p[0]);
        if(ax == 'F') {
            // get speed scale
            scale= strtof(p.substr(1).c_str(), NULL);
            continue;
        }

        if(!((ax >= 'X' && ax <= 'Z') || (ax >= 'A' && ax <= 'C'))) {
            stream->printf("error:bad axis %c\n", ax);
            return;
        }

        uint8_t a= ax >= 'X' ? ax - 'X' : ax - 'A' + 3;
        if(a >= n_motors) {
            stream->printf("error:axis out of range %c\n", ax);
            return;
        }

        delta[a]= strtof(p.substr(1).c_str(), NULL);
    }

    // select slowest axis rate to use
    bool ok= false;
    for (int i = 0; i < n_motors; ++i) {
        if(delta[i] != 0) {
            ok= true;
            if(isnan(rate_mm_s)) {
                rate_mm_s= THEROBOT->actuators[i]->get_max_rate();
            }else{
                rate_mm_s = std::min(rate_mm_s, THEROBOT->actuators[i]->get_max_rate());
            }
            //hstream->printf("%d %f F%f\n", i, delta[i], rate_mm_s);
        }
    }
    if(!ok) {
        stream->printf("error:no delta jog specified\n");
        return;
    }

    //stream->printf("F%f\n", rate_mm_s*scale);

    THEROBOT->delta_move(delta, rate_mm_s*scale, n_motors);
    // turn off queue delay and run it now
    THECONVEYOR->force_queue();
}

void SimpleShell::help_command( string parameters, StreamOutput *stream )
{
    stream->printf("Commands:\r\n");
    stream->printf("version\r\n");
    stream->printf("mem [-v]\r\n");
    stream->printf("ls [-s] [-e] [folder]\r\n");
    stream->printf("cd folder\r\n");
    stream->printf("pwd\r\n");
    stream->printf("cat file [limit] [-e] [-d 10]\r\n");
    stream->printf("rm file [-e]\r\n");
    stream->printf("mv file newfile [-e]\r\n");
    stream->printf("remount\r\n");
    stream->printf("play file [-v]\r\n");
    stream->printf("progress - shows progress of current play\r\n");
    stream->printf("abort - abort currently playing file\r\n");
    stream->printf("reset - reset smoothie\r\n");
    stream->printf("dfu - enter dfu boot loader\r\n");
    stream->printf("break - break into debugger\r\n");
    stream->printf("config-get [<configuration_source>] <configuration_setting>\r\n");
    stream->printf("config-set [<configuration_source>] <configuration_setting> <value>\r\n");
    stream->printf("get [pos|wcs|state|status|fk|ik]\r\n");
    stream->printf("get temp [bed|hotend]\r\n");
    stream->printf("set_temp bed|hotend 185\r\n");
    stream->printf("switch name [value]\r\n");
    stream->printf("net\r\n");
    stream->printf("ap [channel]\r\n");
    stream->printf("wlan [ssid] [password] [-d] [-e]\r\n");
    stream->printf("diagnose\r\n");
    stream->printf("load [file] - loads a configuration override file from soecified name or config-override\r\n");
    stream->printf("save [file] - saves a configuration override file as specified filename or as config-override\r\n");
    stream->printf("upload filename - saves a stream of text to the named file\r\n");
    stream->printf("calc_thermistor [-s0] T1,R1,T2,R2,T3,R3 - calculate the Steinhart Hart coefficients for a thermistor\r\n");
    stream->printf("thermistors - print out the predefined thermistors\r\n");
    stream->printf("md5sum file - prints md5 sum of the given file\r\n");
}

// output all configs
void SimpleShell::config_get_all_command( string parameters, StreamOutput *stream )
{
    // Get parameters ( filename and line limit )
    string filename = "/sd/config.txt";
    bool send_eof = false;
    // parse parameters
    while (parameters != "") {
    	string s = shift_parameter(parameters);
    	if (s == "-e") {
            send_eof = true; // we need to terminate file send with an eof
        } else if (s != "" ) {
        	filename = s;
        }
    }

    string buffer;
    string key, value;
    int c;
	size_t begin_key, end_key, end_value, vsize;
    // Open the config file ( find it if we haven't already found it )
	FILE *lp = fopen(filename.c_str(), "r");
    if (lp == NULL) {
        stream->printf("Config file not found: %s\r\n", filename.c_str());
        return;
    }
	while ((c = fgetc (lp)) != EOF) {
		buffer.append((char *)&c, 1);
		if (c == '\n') {
			// process and send key=value data
		    if( buffer.length() < 3 ) {
		    	buffer.clear();
		        continue;
		    }
		    begin_key = buffer.find_first_not_of(" \t");
		    if (begin_key == string::npos || buffer[begin_key] == '#') {
		    	buffer.clear();
		    	continue;
		    }
		    end_key = buffer.find_first_of(" \t", begin_key);
		    if(end_key == string::npos) {
		    	buffer.clear();
		        continue;
		    }

		    size_t begin_value = buffer.find_first_not_of(" \t", end_key);
		    if(begin_value == string::npos || buffer[begin_value] == '#') {
		    	buffer.clear();
		    	continue;
		    }

		    key = buffer.substr(begin_key,  end_key - begin_key);
		    end_value = buffer.find_first_of("\r\n# \t", begin_value + 1);
		    vsize = (end_value == string::npos) ? end_value : end_value - begin_value;
		    value = buffer.substr(begin_value, vsize);

		    stream->printf("%s=%s\n", key.c_str(), value.c_str());

			buffer.clear();
			// we need to kick things or they die
			THEKERNEL->call_event(ON_IDLE);
		}
	}

    fclose(lp);

    if(send_eof) {
        stream->_putc(EOT);
    }
}

// restore config from default
void SimpleShell::config_restore_command( string parameters, StreamOutput *stream )
{
    // Get parameters ( filename and line limit )
	string current_filename = "/sd/config.txt";
    string default_filename = "/sd/config.default";
    // Open file
    FILE *default_lp = fopen(default_filename.c_str(), "r");
    if (default_lp == NULL) {
        stream->printf("Default file not found: %s\r\n", default_filename.c_str());
        return;
    }
    FILE *current_lp = fopen(current_filename.c_str(), "w");
    if (current_lp == NULL) {
        stream->printf("Config file not found or created fail: %s\r\n", current_filename.c_str());
        return;
    }

    int c;
    // Print each line of the file
    while ((c = fgetc (default_lp)) != EOF) {
    	fputc(c, current_lp);
    };
    fclose(current_lp);
    fclose(default_lp);

    stream->printf("Settings restored complete.\n");
}

// save current config file to default
void SimpleShell::config_default_command( string parameters, StreamOutput *stream )
{
    // Get parameters ( filename and line limit )
	string current_filename = "/sd/config.txt";
    string default_filename = "/sd/config.default";
    // Open file
    FILE *default_lp = fopen(default_filename.c_str(), "w");
    if (default_lp == NULL) {
        stream->printf("Default file not found or created fail: %s\r\n", default_filename.c_str());
        return;
    }
    FILE *current_lp = fopen(current_filename.c_str(), "r");
    if (current_lp == NULL) {
        stream->printf("Config file not found: %s\r\n", current_filename.c_str());
        return;
    }

    int c;
    // Print each line of the file
    while ((c = fgetc (current_lp)) != EOF) {
    	fputc(c, default_lp);
    };
    fclose(current_lp);
    fclose(default_lp);

    stream->printf("Settings save as default complete.\n");
}

void SimpleShell::upload_command( string parameters, StreamOutput *stream )
{
    unsigned char *p;
    char *recv_buff;
    int bufsz, crc = 0, is_stx = 0;
    unsigned char trychar = 'C';
    unsigned char packetno = 1;
    int c, len = 0;
    int retry = 0;
    int retrans = MAXRETRANS;
    int timeouts = MAXRETRANS;
    int recv_count = 0;
    bool md5_received = false;
    uint32_t u32filesize = 0;

    // open file
	char error_msg[64];
	memset(error_msg, 0, sizeof(error_msg));
	sprintf(error_msg, "Nothing!");
    string filename = absolute_from_relative(shift_parameter(parameters));
    string md5_filename = change_to_md5_path(filename);
    string lzfilename = change_to_lz_path(filename);
    check_and_make_path(md5_filename);
    check_and_make_path(lzfilename);

	// diasble serial rx irq in case of serial stream, and internal process in case of wifi
    if (stream->type() == 0) {
    	set_serial_rx_irq(false);
    }
    THEKERNEL->set_uploading(true);

    if (!THECONVEYOR->is_idle()) {
        stream->_putc(EOT);
        if (stream->type() == 0) {
        	set_serial_rx_irq(true);
        }
        THEKERNEL->set_uploading(false);
        return;
    }
	
	//if file is lzCompress file,then need to put .lz dir
	unsigned int start_pos = filename.find(".lz");
	FILE *fd;
	if (start_pos != string::npos) {
		start_pos = lzfilename.rfind(".lz");
		lzfilename=lzfilename.substr(0, start_pos);
    	fd = fopen(lzfilename.c_str(), "wb");
    }
    else {
    	fd = fopen(filename.c_str(), "wb");
    }
		
    FILE *fd_md5 = NULL;
    //if file is lzCompress file,then need to Decompress
	start_pos = md5_filename.find(".lz");
	if (start_pos != string::npos) {
		md5_filename=md5_filename.substr(0, start_pos);
	}
    if (filename.find("firmware.bin") == string::npos) {
    	fd_md5 = fopen(md5_filename.c_str(), "wb");
    }

    if (fd == NULL || (filename.find("firmware.bin") == string::npos && fd_md5 == NULL)) {
        stream->_putc(EOT);
    	sprintf(error_msg, "Error: failed to open file [%s]!\r\n", fd == NULL ? filename.substr(0, 30).c_str() : md5_filename.substr(0, 30).c_str() );
    	goto upload_error;
    }
	
    for (;;) {
        for (retry = 0; retry < MAXRETRANS; ++retry) {  // approx 3 seconds allowed to make connection
            if (trychar)
            	stream->_putc(trychar);
            if ((c = inbyte(stream, TIMEOUT_MS)) >= 0) {
            	retry = 0;
            	switch (c) {
                case SOH:
                    bufsz = 128;
                    is_stx = 0;
                    goto start_recv;
                case STX:
                    bufsz = 8192;
                    is_stx = 1;
                    goto start_recv;
                case EOT:
                    stream->_putc(ACK);
                    flush_input(stream);
                    goto upload_success; /* normal end */
                case CAN:
                    if ((c = inbyte(stream, TIMEOUT_MS)) == CAN) {
                        stream->_putc(ACK);
                        flush_input(stream);
                    	sprintf(error_msg, "Info: Upload canceled by remote!\r\n");
                        goto upload_error;
                    }
                    goto upload_error;
                    break;
                default:
                    break;
                }
            }
			else
			{
				safe_delay_ms(10);
			}
        }

        if (trychar == 'C') {
            trychar = NAK;
            continue;
        }
        cancel_transfer(stream);
		sprintf(error_msg, "Error: upload sync error! get char [%d], retry [%d]!\r\n", c, retry);
        goto upload_error;

    start_recv:
        if (trychar == 'C')
            crc = 1;
        trychar = 0;
        p = xbuff;
        *p++ = c;

        recv_count = 1 + bufsz + (crc ? 1 : 0) + 3 + is_stx;

        timeouts = MAXRETRANS;

        while (recv_count > 0) {
        	c = inbytes(stream, &recv_buff, recv_count, TIMEOUT_MS);
        	if (c < 0) {
        		safe_delay_ms(10);
        		timeouts --;
        		if (timeouts < 0) {
            		goto reject;
        		}
        	} else {
        		timeouts = MAXRETRANS;
            	for (int i = 0; i < c; i ++) {
            		*p++ = recv_buff[i];
            	}
            	recv_count -= c;
        	}
        }

        len = is_stx ? (xbuff[3] << 8 | xbuff[4]) : xbuff[3];
        if (!md5_received && xbuff[1] == 0 && xbuff[1] == (unsigned char)(~xbuff[2])
        		&& check_crc(crc, &xbuff[3], bufsz + 1 + is_stx) && len == 32) {
        	// received md5
        	if (NULL != fd_md5) {
    			fwrite(&xbuff[4 + is_stx], sizeof(char), 32, fd_md5);
        	}
            THEKERNEL->call_event(ON_IDLE);
            stream->_putc(ACK);
            md5_received = true;
            continue;
        } else if (xbuff[1] == (unsigned char)(~xbuff[2]) &&
        		xbuff[1] == packetno && check_crc(crc, &xbuff[3], bufsz + 1 + is_stx)) {

            // Set the file write system buffer 4096 Byte
        	setvbuf(fd, (char*)fbuff, _IOFBF, 4096);
			fwrite(&xbuff[4 + is_stx], sizeof(char), len, fd);
			u32filesize += len;
			++ packetno;
			retrans = MAXRETRANS + 1;
			THEKERNEL->call_event(ON_IDLE);
            stream->_putc(ACK);
            continue;
        }
    reject:
		stream->_putc(NAK);
		if (-- retrans <= 0) {
            cancel_transfer(stream);
        	sprintf(error_msg, "Error: too many retry error!\r\n");
            goto upload_error; /* too many retry error */
		}
    }
upload_error:
	if (fd != NULL) {
		fclose(fd);
		fd = NULL;
		remove(filename.c_str());
	}
	if (fd_md5 != NULL) {
		fclose(fd_md5);
		fd_md5 = NULL;
		remove(md5_filename.c_str());
	}
	flush_input(stream);
    if (stream->type() == 0) {
    	set_serial_rx_irq(true);
    }
    THEKERNEL->set_uploading(false);
	stream->printf(error_msg);
	return;
upload_success:

	if (fd != NULL) {
		fclose(fd);
		fd = NULL;
	}
	if (fd_md5 != NULL) {
		fclose(fd_md5);
		fd_md5 = NULL;
	}
	flush_input(stream);

    THEKERNEL->set_uploading(false);
	//if file is lzCompress file,then need to Decompress
	start_pos = filename.find(".lz");
	string srcfilename=lzfilename;
	string desfilename= filename;
	if (start_pos != string::npos) {
		desfilename=filename.substr(0, start_pos);
		if(!decompress(srcfilename,desfilename,u32filesize,stream))
			goto upload_error;
    }

    if (stream->type() == 0) {
    	set_serial_rx_irq(true);
    }
	stream->printf("Info: upload success: %s.\r\n", desfilename.c_str());
}

void SimpleShell::compute_md5sum_command( string parameters, StreamOutput* stream ) {
    string filename = absolute_from_relative(shift_parameter(parameters));
	FILE *fd = fopen(filename.c_str(), "rb");
	if (NULL != fd) {
        MD5 md5;
        uint8_t md5_buf[64];
        do {
            size_t n = fread(md5_buf, 1, sizeof(md5_buf), fd);
            if (n > 0) md5.update(md5_buf, n);
            THEKERNEL->call_event(ON_IDLE);
        } while (!feof(fd));
        strcpy(md5_str, md5.finalize().hexdigest().c_str());
        fclose(fd);
        fd = NULL;
	}
}

void SimpleShell::download_command( string parameters, StreamOutput *stream )
{
	int bufsz = 8192;
    int crc = 0, is_stx = 1;
    unsigned char packetno = 0;
    int i, c = 0;
    int retry = 0;
    bool resend = true;

    // open file
	char error_msg[64];
	unsigned char md5_sent = 0;
	memset(error_msg, 0, sizeof(error_msg));
    string filename = absolute_from_relative(shift_parameter(parameters));
    string md5_filename = change_to_md5_path(filename);
    string lz_filename = change_to_lz_path(filename);

	// diasble irq
    if (stream->type() == 0) {
    	bufsz = 128;
    	is_stx = 0;
    	set_serial_rx_irq(false);
    }
    THEKERNEL->set_uploading(true);

    if (!THECONVEYOR->is_idle()) {
        cancel_transfer(stream);
        if (stream->type() == 0) {
        	set_serial_rx_irq(true);
        }
        THEKERNEL->set_uploading(false);
        return;
    }

    char md5[64];
    memset(md5, 0, sizeof(md5));

    FILE *fd = fopen(md5_filename.c_str(), "rb");
    if (fd != NULL) {
        fread(md5, sizeof(char), 64, fd);
        fclose(fd);
        fd = NULL;
    } else {
    	strcpy(md5, this->md5_str);
    }
	
	fd = fopen(lz_filename.c_str(), "rb");		//first try to open /.lz/filename
	if (NULL == fd) {	
	    fd = fopen(filename.c_str(), "rb");
	    if (NULL == fd) {
		    cancel_transfer(stream);
			sprintf(error_msg, "Error: failed to open file [%s]!\r\n", filename.substr(0, 30).c_str());
			goto download_error;
	    }
	}
    

    for(;;) {
		for (retry = 0; retry < MAXRETRANS; ++retry) {
			if ((c = inbyte(stream, TIMEOUT_MS)) >= 0) {
				retry = 0;
				switch (c) {
				case 'C':
					crc = 1;
					goto start_trans;
				case NAK:
					crc = 0;
					goto start_trans;
				case CAN:
					if ((c = inbyte(stream, TIMEOUT_MS)) == CAN) {
						stream->_putc(ACK);
						flush_input(stream);
				    	sprintf(error_msg, "Info: canceled by remote!\r\n");
				        goto download_error;
					}
					break;
				default:
					break;
				}
			}
			else
			{
				safe_delay_ms(10);
			}
		}
        cancel_transfer(stream);
		sprintf(error_msg, "Error: download sync error! get char [%02X], retry [%d]!\r\n", c, retry);
        goto download_error;

		for(;;) {
		start_trans:
			if (packetno == 0 && md5_sent == 0) {
				c = strlen(md5);
				memcpy(&xbuff[4 + is_stx], md5, c);
				md5_sent = 1;
			} else {
				c = fread(&xbuff[4 + is_stx], sizeof(char), bufsz, fd);
				if (c <= 0) {
					for (retry = 0; retry < MAXRETRANS; ++retry) {
						stream->_putc(EOT);
						if ((c = inbyte(stream, TIMEOUT_MS)) == ACK) break;
					}
					flush_input(stream);
					if (c == ACK) {
						goto download_success;
					} else {
						sprintf(error_msg, "Error: get finish ACK error!\r\n");
				        goto download_error;
					}
				}
			}
			xbuff[0] = is_stx ? STX : SOH;
			xbuff[1] = packetno;
			xbuff[2] = ~packetno;
			xbuff[3] = is_stx ? c >> 8 : c;
			if (is_stx) {
				xbuff[4] = c & 0xff;
			}
			if (c < bufsz) {
				memset(&xbuff[4 + is_stx + c], CTRLZ, bufsz - c);
			}

			if (crc) {
				unsigned short ccrc = crc16_ccitt(&xbuff[3], bufsz + 1 + is_stx);
				xbuff[bufsz + 4 + is_stx] = (ccrc >> 8) & 0xFF;
				xbuff[bufsz + 5 + is_stx] = ccrc & 0xFF;
			} else {
				unsigned char ccks = 0;
				for (i = 3; i < bufsz + 1 + is_stx; ++i) {
					ccks += xbuff[i];
				}
				xbuff[bufsz + 4 + is_stx] = ccks;
			}

			resend = true;
			for (retry = 0; retry < MAXRETRANS; ++retry) {
				if (resend) {
					stream->puts((char *)xbuff, bufsz + 5 + is_stx + (crc ? 1:0));
					resend = false;
				}
				if ((c = inbyte(stream, TIMEOUT_MS)) >= 0 ) {
					retry = 0;
					switch (c) {
					case ACK:
						++packetno;
						goto start_trans;
					case CAN:
						if ((c = inbyte(stream, TIMEOUT_MS)) == CAN) {
							stream->_putc(ACK);
							flush_input(stream);
					    	sprintf(error_msg, "Info: canceled by remote!\r\n");
					        goto download_error;
						}
						break;
					case NAK:
						resend = true;
					default:
						break;
					}
				}
				else
				{
					safe_delay_ms(500);
				}

			}

	        cancel_transfer(stream);
			sprintf(error_msg, "Error: transmit error, char: [%d], retry: [%d]!\r\n", c, retry);
	        goto download_error;
		}
	}
download_error:
	if (fd != NULL) {
		fclose(fd);
		fd = NULL;
	}
	flush_input(stream);
    if (stream->type() == 0) {
    	set_serial_rx_irq(true);
    }
    THEKERNEL->set_uploading(false);
	stream->printf(error_msg);
	return;
download_success:
	if (fd != NULL) {
		fclose(fd);
		fd = NULL;
	}
	flush_input(stream);
    if (stream->type() == 0) {
    	set_serial_rx_irq(true);
    }
    THEKERNEL->set_uploading(false);
	stream->printf("Info: download success: %s.\r\n", filename.c_str());
	return;
}

int SimpleShell::decompress(string sfilename, string dfilename, uint32_t sfilesize, StreamOutput* stream)
{
	FILE *f_in = NULL, *f_out = NULL;
	uint16_t  u16Sum = 0;
	uint8_t u8ReadBuffer_hdr[BLOCK_HEADER_SIZE] = { 0 };
	uint32_t u32DcmprsSize = 0, u32BlockSize = 0, u32BlockNum = 0, u32TotalDcmprsSize = 0, i = 0,j = 0,k=0;
	qlz_state_decompress s_stDecompressState;
	char error_msg[64];
	memset(error_msg, 0, sizeof(error_msg));
	sprintf(error_msg, "Nothing!");
	char info_msg[64];
	memset(info_msg, 0, sizeof(info_msg));
	sprintf(info_msg, "Nothing!");

	f_in= fopen(sfilename.c_str(), "rb");
	f_out= fopen(dfilename.c_str(), "w+");
	if (f_in == NULL || f_out == NULL)
	{
		sprintf(error_msg, "Error: failed to create file [%s]!\r\n", sfilename.substr(0, 30).c_str());
		goto _exit;
	}
	for(i = 0; i < sfilesize-2; i+= BLOCK_HEADER_SIZE + u32BlockSize)
	{

		fread(u8ReadBuffer_hdr, sizeof(char), BLOCK_HEADER_SIZE, f_in);
		u32BlockSize = u8ReadBuffer_hdr[0] * (1 << 24) + u8ReadBuffer_hdr[1] * (1 << 16) + u8ReadBuffer_hdr[2] * (1 << 8) + u8ReadBuffer_hdr[3];
		if(!u32BlockSize)
		{
			goto _exit;
		}
		fread(xbuff, sizeof(char), u32BlockSize, f_in);
		u32DcmprsSize = qlz_decompress((const char *)xbuff, fbuff, &s_stDecompressState);
		if(!u32DcmprsSize)
		{
			goto _exit;
		}
		for(j = 0; j < u32DcmprsSize; j++)
		{
			u16Sum += fbuff[j];
		}
		// Set the file write system buffer 4096 Byte
		setvbuf(f_out, (char*)&xbuff[4096], _IOFBF, 4096);
		fwrite(fbuff, sizeof(char),u32DcmprsSize, f_out);
		u32TotalDcmprsSize += u32DcmprsSize;
		u32BlockNum += 1;
		if(++k>10)
		{
			k=0;
			THEKERNEL->call_event(ON_IDLE);
			sprintf(info_msg, "#Info: decompart = %u\r\n", u32BlockNum);
			stream->printf(info_msg);
		}
	}
	fread(fbuff, sizeof(char), 2, f_in);
	if(u16Sum != ((fbuff[0] <<8) + fbuff[1]))
	{
		goto _exit;
	}

	if (f_in != NULL)
		fclose(f_in);
	if (f_out!= NULL)
		fclose(f_out);
	sprintf(info_msg, "#Info: decompart = %u\r\n", u32BlockNum);
	stream->printf(info_msg);
	return 1;
_exit:
	if (f_in != NULL)
		fclose(f_in );
	if (f_out != NULL)
		fclose(f_out);
	stream->printf(error_msg);
	return 0;
}
/*
int SimpleShell::compressfile(string sfilename, string dfilename, StreamOutput* stream)
{
	FILE *f_in = NULL, *f_out = NULL;
	uint16_t  u16Sum = 0;	
	uint8_t sumdata[2];
	uint8_t buffer_hdr[BLOCK_HEADER_SIZE] = { 0 };
	uint32_t file_size = 0;	
	uint32_t u32cmprsSize = 0, u32BlockSize = 0, u32TotalCmprsSize = 0, i = 0,k=0;
	qlz_state_compress s_stCompressState;
	char info_msg[64];
//	memset(info_msg, 0, sizeof(info_msg));
//	sprintf(info_msg, "Nothing!");

	f_in= fopen(sfilename.c_str(), "rb");
	f_out= fopen(dfilename.c_str(), "w+");
	if (f_in == NULL || f_out == NULL)
	{
		sprintf(info_msg, "Error: failed to create file [%s]!\r\n", filename.substr(0, 30).c_str());
		goto _exit;
	}
	file_size = ftell(f_in);
	if (file_size == 0)
	{
		sprintf(info_msg, "Error: [qlz] File size = 0\n");
		goto _exit;
	}
	while(feof(f_in))
	{
		u32BlockSize = fread(xbuff, sizeof(char), COMPRESS_BUFFER_SIZE, f_in);
		for(i=0; i< u32BlockSize; i++ )
			u16Sum += xbuff[i];
		/* The destination buffer must be at least size + 400 bytes large because incompressible data may increase in size. */
/*		u32cmprsSize = qlz_compress((const char *)xbuff, (char *)fbuff, u32BlockSize, &s_stCompressState);
		if(!u32cmprsSize)
		{
			goto _exit;
		}
		buffer_hdr[3] = u32cmprsSize % (1 << 8);
		buffer_hdr[2] = (u32cmprsSize % (1 << 16)) / (1 << 8);
		buffer_hdr[1] = (u32cmprsSize % (1 << 24)) / (1 << 16);
		buffer_hdr[0] = u32cmprsSize / (1 << 24); 
 
		fwrite(buffer_hdr, 1,BLOCK_HEADER_SIZE, f_out);
		// Set the file write system buffer 4096 Byte
		setvbuf(f_out, (char*)&xbuff[4096], _IOFBF, 4096);
		fwrite(fbuff, sizeof(char),u32cmprsSize, f_out);
		u32TotalCmprsSize += u32cmprsSize;
		if(++k>100)
		{
			k=0;
			THEKERNEL->call_event(ON_IDLE);
			sprintf(info_msg, "Info: ComSize = %u, Filesize = %u\r\n", u32TotalCmprsSize, file_size);
			stream->printf(info_msg);
		}
	}
	
	sumdata[0] = u16Sum >> 8;
	sumdata[1] = u16Sum &0x00FF; 
	fwrite(sumdata, 1, 2, f_out);
	if(u16Sum != ((fbuff[0] <<8) + fbuff[1]))
	{
		goto _exit;
	}

	if (f_in)
		fclose(f_in);
	if (f_out)
		fclose(f_out);
	sprintf(info_msg, "Info: ComSize = %u, Filesize = %u\r\n", u32TotalCmprsSize, file_size);
	stream->printf(info_msg);
	return 1;
_exit:
	if (f_in)
		fclose(f_in);
	if (f_out)
		fclose(f_out);
	stream->printf(info_msg);
	return 0;
}
*/


unsigned int SimpleShell::crc16_ccitt(unsigned char *data, unsigned int len)
{
	static const unsigned short crc_table[] = {
		0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
		0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
		0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
		0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
		0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
		0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
		0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
		0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
		0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
		0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
		0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
		0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
		0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
		0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
		0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
		0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
		0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
		0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
		0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
		0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
		0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
		0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
		0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
		0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
		0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
		0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
		0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
		0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
		0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
		0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
		0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
		0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0,
	};

	unsigned char tmp;
	unsigned short crc = 0;

	for (unsigned int i = 0; i < len; i ++) {
        tmp = ((crc >> 8) ^ data[i]) & 0xff;
        crc = ((crc << 8) ^ crc_table[tmp]) & 0xffff;
	}

	return crc & 0xffff;
}

int SimpleShell::check_crc(int crc, unsigned char *data, unsigned int len)
{
    if (crc) {
        unsigned short crc = crc16_ccitt(data, len);
        unsigned short tcrc = (data[len] << 8) + data[len+1];
        if (crc == tcrc)
            return 1;
    }
    else {
        unsigned char cks = 0;
        for (unsigned int i = 0; i < len; ++i) {
            cks += data[i];
        }
        if (cks == data[len])
        return 1;
    }

    return 0;
}

int SimpleShell::inbyte(StreamOutput *stream, unsigned int timeout_ms)
{
	uint32_t tick_us = us_ticker_read();
    while (us_ticker_read() - tick_us < timeout_ms * 1000) {
        if (stream->ready())
            return stream->_getc();
        safe_delay_us(100);
    }
    return -1;
}

int SimpleShell::inbytes(StreamOutput *stream, char **buf, int size, unsigned int timeout_ms)
{
	uint32_t tick_us = us_ticker_read();
    while (us_ticker_read() - tick_us < timeout_ms * 1000) {
        if (stream->ready())
            return stream->gets(buf, size);
        safe_delay_us(100);
    }
    return -1;
}

void SimpleShell::flush_input(StreamOutput *stream)
{
    while (inbyte(stream, TIMEOUT_MS) >= 0)
        continue;
}

void SimpleShell::cancel_transfer(StreamOutput *stream)
{
	stream->_putc(CAN);
	stream->_putc(CAN);
	stream->_putc(CAN);
	flush_input(stream);
}

void SimpleShell::set_serial_rx_irq(bool enable)
{
	// disable serial rx irq
    bool enable_irq = enable;
    PublicData::set_value( atc_handler_checksum, set_serial_rx_irq_checksum, &enable_irq );
}
