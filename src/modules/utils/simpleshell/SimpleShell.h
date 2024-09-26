/*
      This file is part of Smoothie (http://smoothieware.org/). The motion control part is heavily based on Grbl (https://github.com/simen/grbl).
      Smoothie is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
      Smoothie is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
      You should have received a copy of the GNU General Public License along with Smoothie. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "Module.h"
#include "XModem.h"

#include <functional>
#include <string>
#include <cstdint>

using std::string;

class StreamOutput;

class SimpleShell : public Module
{
public:
    SimpleShell() {}

    void on_module_loaded();
    void on_console_line_received( void *argument );
    void on_gcode_received(void *argument);
    void on_second_tick(void *);
    bool parse_command(const char *cmd, string args, StreamOutput *stream);
    void print_mem(StreamOutput *stream) { mem_command("", stream); }
    void version_command(string parameters, StreamOutput *stream );
    void ftype_command( string parameters, StreamOutput *stream );

private:

    void jog(string params, StreamOutput *stream);

    void ls_command(string parameters, StreamOutput *stream );
    void cd_command(string parameters, StreamOutput *stream );
    void delete_file_command(string parameters, StreamOutput *stream );
    void pwd_command(string parameters, StreamOutput *stream );
    void upload_command(string parameters, StreamOutput *stream);
    void download_command(string parameters, StreamOutput *stream);
    void compute_md5sum_command(string parameters, StreamOutput *stream);
    void cat_command(string parameters, StreamOutput *stream );
    void echo_command(string parameters, StreamOutput *stream );
    void rm_command(string parameters, StreamOutput *stream );
    void mv_command(string parameters, StreamOutput *stream );
    void mkdir_command(string parameters, StreamOutput *stream );
    void break_command(string parameters, StreamOutput *stream );
    void reset_command(string parameters, StreamOutput *stream );
    void dfu_command(string parameters, StreamOutput *stream );
    void help_command(string parameters, StreamOutput *stream );
    void get_command(string parameters, StreamOutput *stream );
    void set_temp_command(string parameters, StreamOutput *stream );
    void calc_thermistor_command( string parameters, StreamOutput *stream);
    void print_thermistors_command( string parameters, StreamOutput *stream);
    void md5sum_command( string parameters, StreamOutput *stream);
    void grblDP_command( string parameters, StreamOutput *stream);

    void switch_command(string parameters, StreamOutput *stream );
    void mem_command(string parameters, StreamOutput *stream );

    void net_command( string parameters, StreamOutput *stream);
    void ap_command( string parameters, StreamOutput *stream);
    void wlan_command( string parameters, StreamOutput *stream);
    void diagnose_command( string parameters, StreamOutput *stream);
    void sleep_command( string parameters, StreamOutput *stream);
    void power_command( string parameters, StreamOutput *stream);

    void load_command( string parameters, StreamOutput *stream);
    void save_command( string parameters, StreamOutput *stream);

    void remount_command( string parameters, StreamOutput *stream);

    void test_command( string parameters, StreamOutput *stream);

    void time_command( string parameters, StreamOutput *stream);

    void config_get_all_command(string parameters, StreamOutput *stream );

    void config_restore_command(string parameters, StreamOutput *stream );

    void config_default_command(string parameters, StreamOutput *stream );

    typedef void (*PFUNC)(string parameters, StreamOutput *stream);
    typedef struct {
        const char* name;
        void (SimpleShell::*command)(std::string parameters, StreamOutput* stream);
    } ptentry_t;

    static const ptentry_t commands_table[];
    static int reset_delay_secs;

    XModem xmodem;

    char md5_str[64];
};
