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
#include "XModem.h"
#include "utils.h"

#include "mbed.h" // for wait_ms()

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

extern unsigned int g_maximumHeapAddress;

#include <malloc.h>
#include <mri.h>
#include <stdio.h>
#include <stdint.h>
#include <functional>

extern "C" uint32_t  __end__;
extern "C" uint32_t  __malloc_free_list;
extern "C" caddr_t   _sbrk(int size);

#define EOT 4
#define CAN 24


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

// Adam Greens heap walk from http://mbed.org/forum/mbed/topic/2701/?page=4#comment-22556
static uint32_t heapWalk(StreamOutput *stream, bool verbose)
{
    uint32_t chunkNumber = 1;
    // The __end__ linker symbol points to the beginning of the heap.
    uint32_t chunkCurr = (uint32_t)&__end__;
    // __malloc_free_list is the head pointer to newlib-nano's link list of free chunks.
    uint32_t freeCurr = __malloc_free_list;
    // Calling _sbrk() with 0 reserves no more memory but it returns the current top of heap.
    uint32_t heapEnd = (uint32_t)_sbrk(0);
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

void SimpleShell::system_reset_callback()
{
    system_reset(false);
}

void SimpleShell::on_module_loaded()
{
    this->register_for_event(ON_CONSOLE_LINE_RECEIVED);
    this->register_for_event(ON_GCODE_RECEIVED);
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
            if(!args.empty() && !THEKERNEL.is_grbl_mode())
                rm_command("/sd/" + args, gcode->stream);
        } else if (gcode->m == 331) { // change to vacuum mode
			THEKERNEL.set_vacuum_mode(true);
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
			THEKERNEL.set_vacuum_mode(false);
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
			THEKERNEL.set_optional_stop_mode(false);
			// turn off optional stop mode
			gcode->stream->printf("turning optional stop mode off\r\n");
		} else if (gcode->m == 334) { // turn off optional stop mode
			THEKERNEL.set_optional_stop_mode(true);
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
                if(THEKERNEL.is_halted()) {
                    THEKERNEL.call_event(ON_HALT, (void *)1); // clears on_halt
                    new_message.stream->printf("[Caution: Unlocked]\nok\n");
                }
                break;

            case '#':
                grblDP_command("", new_message.stream);
                new_message.stream->printf("ok\n");
                break;

            case 'H':
                if(THEKERNEL.is_halted()) THEKERNEL.call_event(ON_HALT, (void *)1); // clears on_halt
                if(THEKERNEL.is_grbl_mode()) {
                    // issue G28.2 which is force homing cycle
                    Gcode gcode("G28.2", new_message.stream);
                    THEKERNEL.call_event(ON_GCODE_RECEIVED, &gcode);
                }else{
                    Gcode gcode("G28", new_message.stream);
                    THEKERNEL.call_event(ON_GCODE_RECEIVED, &gcode);
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
            configurator.config_get_command(  possible_command, new_message.stream );

        } else if (cmd == "config-set"){
            configurator.config_set_command(  possible_command, new_message.stream );

        } else if (cmd == "config-load"){
            configurator.config_load_command(  possible_command, new_message.stream );

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
void SimpleShell::ls_command(string parameters, StreamOutput *stream)
{
    string path, opts;
    while (!parameters.empty()) {
        string s = shift_parameter(parameters);
        if (s.front() == '-') {
            opts.append(s);
        } else {
            path = s;
            if (!parameters.empty()) {
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
    char dirTmp[256]; // Local buffer
    d = opendir(path.c_str());

    if (d != NULL) {
        while ((p = readdir(d)) != NULL) {
            if (p->d_name[0] == '.') {
                continue;
            }

            for (int i = 0; i < NAME_MAX; i++) {
                if (p->d_name[i] == ' ') p->d_name[i] = 0x01;
            }

            if (opts.find("-s", 0, 2) != string::npos) {
                get_fftime(p->d_date, p->d_time, &timeinfo);
                // Name, size, and date
                snprintf(dirTmp, sizeof(dirTmp), "%s%s %d %04d%02d%02d%02d%02d%02d\r\n",
                         string(p->d_name).c_str(), p->d_isdir ? "/" : "",
                         p->d_isdir ? 0 : p->d_fsize, timeinfo.tm_year + 1980, timeinfo.tm_mon, timeinfo.tm_mday,
                         timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            } else {
                // Only name
                snprintf(dirTmp, sizeof(dirTmp), "%s%s\r\n", string(p->d_name).c_str(), p->d_isdir ? "/" : "");
            }

            stream->puts(dirTmp); // Send each entry directly
        }

        closedir(d);

        if (opts.find("-e", 0, 2) != string::npos) {
            char eot = EOT;
            stream->puts(&eot, 1);
        }
    } else {
        if(opts.find("-e", 0, 2) != string::npos) {
            stream->putc(CAN);
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
            stream->putc(CAN);
        }
    	stream->printf("Could not delete %s \r\n", toRemove.c_str());
    } else {
    	string str_md5 = absolute_from_relative(md5_path);
    	s = remove(str_md5.c_str());
/*
		if (s != 0) {
			if(send_eof) {
				stream->putc(CAN);
			}
			stream->printf("Could not delete %s \r\n", str_md5.c_str());
		} 
		else {
			string str_lz = absolute_from_relative(lz_path);
			s = remove(str_lz.c_str());
			if (s != 0){
				if(send_eof) {
					stream->putc(CAN);
				}
				stream->printf("Could not delete %s \r\n", str_lz.c_str());
			}
			else {
		        if(send_eof) {
		            stream->putc(EOT);
	        	}
			
			}
    	}*/
    	string str_lz = absolute_from_relative(lz_path);
		s = remove(str_lz.c_str());
		if(send_eof) {
            stream->putc(EOT);
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
    		stream->putc(CAN);
    	}
    	stream->printf("Could not rename %s to %s\r\n", from.c_str(), to.c_str());
    } else  {
    	s = rename(md5_from.c_str(), md5_to.c_str());
/*        if (s != 0)  {
        	if (send_eof) {
        		stream->putc(CAN);
        	}
        	stream->printf("Could not rename %s to %s\r\n", md5_from.c_str(), md5_to.c_str());
        }
        else {
        	s = rename(lz_from.c_str(), lz_to.c_str());
        	if (s != 0)  {
	        	if (send_eof) {
	        		stream->putc(CAN);
	        	}
	        	stream->printf("Could not rename %s to %s\r\n", lz_from.c_str(), lz_to.c_str());
        	}
        	else {
        		if (send_eof) {
				stream->putc(EOT);
				}
				stream->printf("renamed %s to %s\r\n", from.c_str(), to.c_str());
        	}
        }*/
        s = rename(lz_from.c_str(), lz_to.c_str());
        if (send_eof) {
			stream->putc(EOT);
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
    		stream->putc(CAN); // ^Z terminates error
    	}
    	stream->printf("could not create directory %s\r\n", path.c_str());
    } else {
    	result = mkdir(md5_path.c_str(), 0);
/*        if (result != 0) {
        	if (send_eof) {
        		stream->putc(CAN); // ^Z terminates error
        	}
        	stream->printf("could not create md5 directory %s\r\n", md5_path.c_str());
        } 
        else if (mkdir(lz_path.c_str(), 0) != 0) {
        	if (send_eof) {
        		stream->putc(CAN); // ^Z terminates error
        	}
        	stream->printf("could not create lz directory %s\r\n", lz_path.c_str());
        }    
        else {
        	if (send_eof) {
            	stream->putc(EOT); // ^D terminates the upload
        	}
        	stream->printf("created directory %s\r\n", path.c_str());
        }
*/
		mkdir(lz_path.c_str(), 0);
		if (send_eof) {
            	stream->putc(EOT); // ^D terminates the upload
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
        THEKERNEL.current_path = folder;
        closedir(d);
    }
}

// Responds with the present working directory
void SimpleShell::pwd_command( string parameters, StreamOutput *stream )
{
    stream->printf("%s\r\n", THEKERNEL.current_path.c_str());
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
            THEKERNEL.call_event(ON_IDLE);
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
        filename = THEKERNEL.config_override_filename();
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
            THEKERNEL.call_event(ON_GCODE_RECEIVED, gcode);
            delete gcode;
            THEKERNEL.call_event(ON_IDLE);
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
        filename = THEKERNEL.config_override_filename();
    }

    THECONVEYOR.wait_for_idle(); //just to be safe as it can take a while to run

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
    THEKERNEL.call_event(ON_GCODE_RECEIVED, gcode );
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
    	stream->printf("time = %lld\n", old_time);
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
            	stream->putc(EOT);
        	}

        } else {
        	if (send_eof) {
        		stream->putc(CAN);
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
            		stream->putc(CAN);
            	}
        	} else {
        		if (t.disconnect) {
            		stream->printf("Wifi Disconnected!\n");
        		} else {
            		stream->printf("Wifi connected, ip: %s\n", t.ip_address);
        		}
            	if (send_eof) {
                	stream->putc(EOT);
            	}
        	}
        } else {
            stream->printf("%s\n", "Parameter error when setting wlan!");
        	if (send_eof) {
        		stream->putc(CAN);
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
	THEKERNEL.set_sleeping(true);
	THEKERNEL.call_event(ON_HALT, nullptr);
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

    resetTimer.start();
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

    std::vector<Robot::wcs_t> v= THEROBOT.get_wcs_state();
    if(verbose) {
        char current_wcs= std::get<0>(v[0]);
        stream->printf("[current WCS: %s]\n", wcs2gcode(current_wcs).c_str());
    }

    int n= std::get<1>(v[0]);
    for (int i = 1; i <= n; ++i) {
        stream->printf("[%s:%1.4f,%1.4f,%1.4f]\n", wcs2gcode(i-1).c_str(),
            THEROBOT.from_millimeters(std::get<0>(v[i])),
            THEROBOT.from_millimeters(std::get<1>(v[i])),
            THEROBOT.from_millimeters(std::get<2>(v[i])));
    }

    float *rd;
    PublicData::get_value( endstops_checksum, g28_position_checksum, &rd );
    stream->printf("[G28:%1.4f,%1.4f,%1.4f]\n",
        THEROBOT.from_millimeters(rd[0]),
        THEROBOT.from_millimeters(rd[1]),
        THEROBOT.from_millimeters(rd[2]));

    stream->printf("[G30:%1.4f,%1.4f,%1.4f]\n", 0.0, 0.0, 0.0); // not supported

    stream->printf("[G92:%1.4f,%1.4f,%1.4f]\n",
        THEROBOT.from_millimeters(std::get<0>(v[n+1])),
        THEROBOT.from_millimeters(std::get<1>(v[n+1])),
        THEROBOT.from_millimeters(std::get<2>(v[n+1])));

    if(verbose) {
        stream->printf("[Tool Offset:%1.4f,%1.4f,%1.4f]\n",
            THEROBOT.from_millimeters(std::get<0>(v[n+2])),
            THEROBOT.from_millimeters(std::get<1>(v[n+2])),
            THEROBOT.from_millimeters(std::get<2>(v[n+2])));
    }else{
        stream->printf("[TL0:%1.4f]\n", THEROBOT.from_millimeters(std::get<2>(v[n+2])));
    }

    // this is the last probe position, updated when a probe completes, also stores the number of steps moved after a homing cycle
    float px, py, pz;
    uint8_t ps;
    std::tie(px, py, pz, ps) = THEROBOT.get_last_probe_position();
    stream->printf("[PRB:%1.4f,%1.4f,%1.4f:%d]\n", THEROBOT.from_millimeters(px), THEROBOT.from_millimeters(py), THEROBOT.from_millimeters(pz), ps);
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
            THEROBOT.arm_solution->actuator_to_cartesian(apos, pos);
            stream->printf("cartesian= X %f, Y %f, Z %f\n", pos[0], pos[1], pos[2]);
            x= pos[0];
            y= pos[1];
            z= pos[2];

        }else{
            // do inverse kinematics on the given cartesian position and display the actuator coordinates
            float pos[3]{x, y, z};
            ActuatorCoordinates apos;
            THEROBOT.arm_solution->cartesian_to_actuator(pos, apos);
            stream->printf("actuator= X %f, Y %f, Z %f\n", apos[0], apos[1], apos[2]);
        }

        if(move) {
            // move to the calculated, or given, XYZ
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "G53 G0 X%f Y%f Z%f", THEROBOT.from_millimeters(x), THEROBOT.from_millimeters(y), THEROBOT.from_millimeters(z));
            struct SerialMessage message;
            message.message = cmd;
            message.stream = &(StreamOutput::NullStream);
            message.line = 0;
            THEKERNEL.call_event(ON_CONSOLE_LINE_RECEIVED, &message );
            THECONVEYOR.wait_for_idle();
        }

   } else if (what == "pos") {
        // convenience to call all the various M114 variants, shows ABC axis where relevant
        std::string buf;
        THEROBOT.print_position(0, buf); stream->printf("last %s\n", buf.c_str()); buf.clear();
        THEROBOT.print_position(1, buf); stream->printf("realtime %s\n", buf.c_str()); buf.clear();
        THEROBOT.print_position(2, buf); stream->printf("%s\n", buf.c_str()); buf.clear();
        THEROBOT.print_position(3, buf); stream->printf("%s\n", buf.c_str()); buf.clear();
        THEROBOT.print_position(4, buf); stream->printf("%s\n", buf.c_str()); buf.clear();
        THEROBOT.print_position(5, buf); stream->printf("%s\n", buf.c_str()); buf.clear();

    } else if (what == "wcs") {
        // print the wcs state
        grblDP_command("-v", stream);

    } else if (what == "state") {
        // also $G and $I
        // [G0 G54 G17 G21 G90 G94 M0 M5 M9 T0 F0.]
        stream->printf("[G%d %s G%d G%d G%d G94 M0 M%c M%c T%d F%1.4f S%1.4f]\n",
            gcode_dispatch.get_modal_command(),
            wcs2gcode(THEROBOT.get_current_wcs()).c_str(),
            THEROBOT.plane_axis_0 == X_AXIS && THEROBOT.plane_axis_1 == Y_AXIS && THEROBOT.plane_axis_2 == Z_AXIS ? 17 :
              THEROBOT.plane_axis_0 == X_AXIS && THEROBOT.plane_axis_1 == Z_AXIS && THEROBOT.plane_axis_2 == Y_AXIS ? 18 :
              THEROBOT.plane_axis_0 == Y_AXIS && THEROBOT.plane_axis_1 == Z_AXIS && THEROBOT.plane_axis_2 == X_AXIS ? 19 : 17,
            THEROBOT.inch_mode ? 20 : 21,
            THEROBOT.absolute_mode ? 90 : 91,
            get_switch_state("spindle") ? '3' : '5',
            get_switch_state("mist") ? '7' : get_switch_state("flood") ? '8' : '9',
            get_active_tool(),
            THEROBOT.from_millimeters(THEROBOT.get_feed_rate()),
            THEROBOT.get_s_value());

    } else if (what == "status") {
        // also ? on serial and usb
        stream->printf("%s\n", THEKERNEL.get_query_string().c_str());

    } else if (what == "compensation") {
    	float mpos[3];
    	THEROBOT.get_current_machine_position(mpos);
    	float old_mpos[3];
    	memcpy(old_mpos, mpos, sizeof(mpos));
		// current_position/mpos includes the compensation transform so we need to get the inverse to get actual position
		if(THEROBOT.compensationTransform) THEROBOT.compensationTransform(mpos, true, true); // get inverse compensation transform
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
            THEKERNEL.call_event(ON_GCODE_RECEIVED, &gcode );
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
		THEKERNEL.call_event(ON_IDLE);
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
        float f= speed.empty() ? THEROBOT.get_feed_rate() : strtof(speed.c_str(), NULL);
        uint32_t n= strtol(iters.c_str(), NULL, 10);

        bool toggle= false;
        for (uint32_t i = 0; i < n; ++i) {
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "G91 G0 %c%f F%f G90", toupper(axis[0]), toggle ? -d : d, f);
            stream->printf("%s\n", cmd);
            struct SerialMessage message{&StreamOutput::NullStream, cmd, 0};
            THEKERNEL.call_event(ON_CONSOLE_LINE_RECEIVED, &message );
            if(THEKERNEL.is_halted()) break;
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
        float f= speed.empty() ? THEROBOT.get_feed_rate() : strtof(speed.c_str(), NULL);

        THEROBOT.push_state();
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "G91 G0 X%f F%f G90", -r, f);
        stream->printf("%s\n", cmd);
        struct SerialMessage message{&StreamOutput::NullStream, cmd, 0};
        THEKERNEL.call_event(ON_CONSOLE_LINE_RECEIVED, &message );

        for (uint32_t i = 0; i < n; ++i) {
            if(THEKERNEL.is_halted()) break;
            snprintf(cmd, sizeof(cmd), "G2 I%f J0 F%f", r, f);
            stream->printf("%s\n", cmd);
            message.message= cmd;
            message.line = 0;
            THEKERNEL.call_event(ON_CONSOLE_LINE_RECEIVED, &message );
        }

        // leave it where it started
        if(!THEKERNEL.is_halted()) {
            snprintf(cmd, sizeof(cmd), "G91 G0 X%f F%f G90", r, f);
            stream->printf("%s\n", cmd);
            struct SerialMessage message{&StreamOutput::NullStream, cmd, 0};
            THEKERNEL.call_event(ON_CONSOLE_LINE_RECEIVED, &message );
        }

        THEROBOT.pop_state();
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
        float f= speed.empty() ? THEROBOT.get_feed_rate() : strtof(speed.c_str(), NULL);
        uint32_t n= strtol(iters.c_str(), NULL, 10);

        for (uint32_t i = 0; i < n; ++i) {
            char cmd[64];
            {
                snprintf(cmd, sizeof(cmd), "G91 G0 X%f F%f", d, f);
                stream->printf("%s\n", cmd);
                struct SerialMessage message{&StreamOutput::NullStream, cmd, 0};
                THEKERNEL.call_event(ON_CONSOLE_LINE_RECEIVED, &message );
            }
            {
                snprintf(cmd, sizeof(cmd), "G0 Y%f", d);
                stream->printf("%s\n", cmd);
                struct SerialMessage message{&StreamOutput::NullStream, cmd, 0};
                THEKERNEL.call_event(ON_CONSOLE_LINE_RECEIVED, &message );
            }
            {
                snprintf(cmd, sizeof(cmd), "G0 X%f", -d);
                stream->printf("%s\n", cmd);
                struct SerialMessage message{&StreamOutput::NullStream, cmd, 0};
                THEKERNEL.call_event(ON_CONSOLE_LINE_RECEIVED, &message );
            }
            {
                snprintf(cmd, sizeof(cmd), "G0 Y%f G90", -d);
                stream->printf("%s\n", cmd);
                struct SerialMessage message{&StreamOutput::NullStream, cmd, 0};
                THEKERNEL.call_event(ON_CONSOLE_LINE_RECEIVED, &message );
            }
            if(THEKERNEL.is_halted()) break;
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

        if(a >= THEROBOT.get_number_registered_motors()) {
            stream->printf("error: axis is out of range\n");
            return;
        }

        uint32_t sps= strtol(stepspersec.c_str(), NULL, 10);
        sps= std::max(sps, 1UL);

        uint32_t delayus= 1000000.0F / sps;
        
        vTaskSuspendAll();
        for(int s= 0;s<steps;s++) {
            if(THEKERNEL.is_halted()) break;
            THEROBOT.actuators[a]->manual_step(dir);

            uint32_t start = us_ticker_read();
            while ((us_ticker_read() - start) < delayus) 
                ;
        }
        xTaskResumeAll();

        // reset the position based on current actuator position
        THEROBOT.reset_position_from_current_actuator_position();

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
    int n_motors= THEROBOT.get_number_registered_motors();

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
                rate_mm_s= THEROBOT.actuators[i]->get_max_rate();
            }else{
                rate_mm_s = std::min(rate_mm_s, THEROBOT.actuators[i]->get_max_rate());
            }
            //hstream->printf("%d %f F%f\n", i, delta[i], rate_mm_s);
        }
    }
    if(!ok) {
        stream->printf("error:no delta jog specified\n");
        return;
    }

    //stream->printf("F%f\n", rate_mm_s*scale);

    THEROBOT.delta_move(delta, rate_mm_s*scale, n_motors);
    // turn off queue delay and run it now
    THECONVEYOR.force_queue();
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
			THEKERNEL.call_event(ON_IDLE);
		}
	}

    fclose(lp);

    if(send_eof) {
        stream->putc(EOT);
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

void SimpleShell::upload_command(std::string parameters, StreamOutput* stream) {
    std::string filename = absolute_from_relative(shift_parameter(parameters));

    bool ret = xmodem.upload(filename, stream);

    if (ret) {
        stream->printf("Info: upload success: %s.\r\n", filename.c_str());
    } else {
        stream->printf("Upload failed for file: %s.\r\n", filename.c_str());
    }

}

void SimpleShell::download_command( string parameters, StreamOutput *stream )
{
    std::string filename = absolute_from_relative(shift_parameter(parameters));

    bool ret = xmodem.download(filename, stream);

    if (ret) {
        stream->printf("Info: Download success: %s.\r\n", filename.c_str());
    } else {
        stream->printf("Download failed for file: %s.\r\n", filename.c_str());
    }
}