/*
      This file is part of Smoothie (http://smoothieware.org/). The motion control part is heavily based on Grbl (https://github.com/simen/grbl).
      Smoothie is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
      Smoothie is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
      You should have received a copy of the GNU General Public License along with Smoothie. If not, see <http://www.gnu.org/licenses/>.
*/

#include "libs/Kernel.h"
#include "libs/utils.h"
#include "system_LPC17xx.h"
#include "LPC17xx.h"
#include "utils.h"

#include <string>
#include <cstring>
#include <stdio.h>
#include <cstdlib>

#include "mbed.h"

#include "FreeRTOS.h"
#include "task.h"

using std::string;

uint16_t get_checksum(const string &to_check)
{
    return get_checksum(to_check.c_str());
}

uint16_t get_checksum(const char *to_check)
{
    // From: http://en.wikipedia.org/wiki/Fletcher%27s_checksum
    uint16_t sum1 = 0;
    uint16_t sum2 = 0;
    const char *p = to_check;
    char c;
    while((c = *p++) != 0) {
        sum1 = (sum1 + c) % 255;
        sum2 = (sum2 + sum1) % 255;
    }
    return (sum2 << 8) | sum1;
}

void get_checksums(uint16_t check_sums[], const string &key)
{
    check_sums[0] = 0x0000;
    check_sums[1] = 0x0000;
    check_sums[2] = 0x0000;
    size_t begin_key = 0;
    unsigned int counter = 0;
    while( begin_key < key.size() && counter < 3 ) {
        size_t end_key =  key.find_first_of(".", begin_key);
        string key_node;
        if(end_key == string::npos) {
            key_node = key.substr(begin_key);
        } else {
            key_node = key.substr(begin_key, end_key - begin_key);
        }

        check_sums[counter] = get_checksum(key_node);
        if(end_key == string::npos) break;
        begin_key = end_key + 1;
        counter++;
    }
}

bool is_alpha(int c)
{
    if ((c >= 'a') && (c <= 'z')) return true;
    if ((c >= 'A') && (c <= 'Z')) return true;
    if ((c == '_')) return true;
    return false;
}

bool is_digit(int c)
{
    if ((c >= '0') && (c <= '9')) return true;
    return false;
}

bool is_numeric(int c)
{
    if (is_digit(c)) return true;
    if ((c == '.') || (c == '-')) return true;
    if ((c == 'e')) return true;
    return false;
}

bool is_alphanum(int c)
{
    return is_alpha(c) || is_numeric(c);
}

bool is_whitespace(int c)
{
    if ((c == ' ') || (c == '\t')) return true;
    return false;
}

// Convert to lowercase
string lc(const string &str)
{
    string lcstr;
    for (auto c : str) {
        lcstr.append(1, ::tolower(c));
    }
    return lcstr;
}

// Remove non-number characters
string remove_non_number( string str )
{
    string number_mask = "0123456789-.abcdefpxABCDEFPX";
    size_t found = str.find_first_not_of(number_mask);
    while (found != string::npos) {
        //str[found]='*';
        str.replace(found, 1, "");
        found = str.find_first_not_of(number_mask);
    }
    return str;
}

// Get the first parameter, and remove it from the original string
string shift_parameter( string &parameters )
{
    size_t beginning = parameters.find_first_of(" ");
    if ( beginning == string::npos ) {
        string temp = parameters;
        parameters = "";
        for (unsigned int i = 0; i < temp.length(); i ++) {
        	if (temp[i] == 0x01) {
        		temp[i] = ' ';
        	} else if (temp[i] == 0x02) {
        		temp[i] = '?';
        	} else if (temp[i] == 0x03) {
        		temp[i] = '*';
        	} else if (temp[i] == 0x04) {
        		temp[i] = '!';
        	} else if (temp[i] == 0x05) {
        		temp[i] = '~';
        	}
        }
        return temp;
    }
    string temp = parameters.substr( 0, beginning );
    parameters = parameters.substr(beginning + 1, parameters.size());
    for (unsigned int i = 0; i < temp.length(); i ++) {
    	if (temp[i] == 0x01) {
    		temp[i] = ' ';
    	} else if (temp[i] == 0x02) {
    		temp[i] = '?';
    	} else if (temp[i] == 0x03) {
    		temp[i] = '*';
    	} else if (temp[i] == 0x04) {
    		temp[i] = '!';
    	} else if (temp[i] == 0x05) {
    		temp[i] = '~';
    	}
    }
    return temp;
}

// Separate command from arguments
string get_arguments( const string& possible_command )
{
    size_t beginning = possible_command.find_first_of(" ");
    if( beginning == string::npos ) {
        return "";
    }
    return possible_command.substr( beginning + 1, possible_command.size() - beginning + 1);
}

// Returns true if the file exists
bool file_exists( const string file_name )
{
    bool exists = false;
    FILE *lp = fopen(file_name.c_str(), "r");
    if(lp) {
        exists = true;
    }
    fclose(lp);
    return exists;
}

// Prepares and executes a watchdog reset for dfu or reboot
void system_reset( bool dfu )
{
    if(dfu) {
        LPC_WDT->WDCLKSEL = 0x1;                // Set CLK src to PCLK
        uint32_t clk = SystemCoreClock / 16;    // WD has a fixed /4 prescaler, PCLK default is /4
        LPC_WDT->WDTC = 1 * (float)clk;         // Reset in 1 second
        LPC_WDT->WDMOD = 0x3;                   // Enabled and Reset
        LPC_WDT->WDFEED = 0xAA;                 // Kick the dog!
        LPC_WDT->WDFEED = 0x55;
    } else {
        NVIC_SystemReset();
    }
}

// Convert a path indication ( absolute or relative ) into a path ( absolute )
std::string absolute_from_relative( std::string path )
{
    string cwd = THEKERNEL.current_path;

    if ( path.empty() ) {
        return THEKERNEL.current_path;
    }

    if ( path[0] == '/' ) {
        return path;
    }

    while ( path.substr(0, 3) == "../" ) {
        path = path.substr(3);
        unsigned found = cwd.find_last_of("/");
        cwd = cwd.substr(0, found);
    }

    if ( path.substr(0, 2) == ".." ) {
        path = path.substr(2);
        unsigned found = cwd.find_last_of("/");
        cwd = cwd.substr(0, found);
    }

    if ( cwd[cwd.length() - 1] == '/' ) {
        return cwd + path;
    }

    return cwd + '/' + path;
}

// Change from origin path to sub path
std::string change_to_md5_path( std::string origin )
{
	unsigned found = origin.find("gcodes/");
	string filename = origin.substr(found + 7);
	string path = "/sd/gcodes";
	DIR * d = opendir(path.c_str());
	if(NULL == d )
	{
		mkdir(path.c_str(), 0);
	}
	else
	{
		closedir(d);	
	}
	path = "/sd/gcodes/.md5";
	d = opendir(path.c_str());
	if(NULL == d )
	{
		mkdir(path.c_str(), 0);
	}
	else
	{
		closedir(d);	
	}
	
	return "/sd/gcodes/.md5/" + filename;
}

// Change from origin path to quicklz file sub path
std::string change_to_lz_path( std::string origin )
{
	unsigned found = origin.find("gcodes/");
	string filename = origin.substr(found + 7);	
	string path = "/sd/gcodes/.lz";
	DIR * d = opendir(path.c_str());
	if(NULL == d )
	{
		mkdir(path.c_str(), 0);
	}
	else
	{
		closedir(d);	
	}
	return "/sd/gcodes/.lz/" + filename;
}


// Check the quicklz/md5 file path
#define	FR_OK 0
#define FR_EXIST 8
void check_and_make_path( std::string origin )
{
	size_t pos = 0;
    std::string dir;

    while ((pos = origin.find_first_of('/', pos)) != std::string::npos) {
        dir = origin.substr(0, pos++);
        if (dir.empty()) continue;  // Skip leading '/'
        mkdir(dir.c_str(),0);
    }
}

// FIXME this does not handle empty strings correctly
//split a string on a delimiter, return a vector of the split tokens
vector<string> split(const char *str, char c)
{
    vector<string> result;

    do {
        const char *begin = str;

        while(*str != c && *str)
            str++;

        result.push_back(string(begin, str));
    } while (0 != *str++);

    return result;
}

// FIXME this does not handle empty strings correctly
// parse a number list "1.1,2.2,3.3" and return the numbers in a vector of floats
vector<float> parse_number_list(const char *str)
{
    vector<string> l= split(str, ',');
    vector<float> r;
    for(auto& s : l){
        float x = strtof(s.c_str(), nullptr);
        r.push_back(x);
    }
    return r;
}

vector<uint32_t> parse_number_list(const char *str, uint8_t radix)
{
    vector<string> l= split(str, ',');
    vector<uint32_t> r;
    for(auto& s : l){
        uint32_t x = strtol(s.c_str(), nullptr, radix);
        r.push_back(x);
    }
    return r;
}

int append_parameters(char *buf, std::vector<std::pair<char,float>> params, size_t bufsize)
{
    size_t n= 0;
    for(auto &i : params) {
        if(n >= bufsize) break;
        buf[n++]= i.first;
        n += snprintf(&buf[n], bufsize-n, "%1.4f ", i.second);
    }
    return n;
}

string wcs2gcode(int wcs) {
    string str= "G5";
    str.append(1, std::min(wcs, 5) + '4');
    if(wcs >= 6) {
        str.append(".").append(1, '1' + (wcs - 6));
    }
    return str;
}

void safe_delay_ms(uint32_t delay)
{
    vTaskDelay(pdMS_TO_TICKS(delay));
}

struct tm *get_fftime(unsigned short t_date, unsigned short t_time, struct tm *timeinfo) {

	// uint16_t mask = (1 << (end - begin + 1)) - 1;
	// return (value >> begin) & mask;

    // Setup a tm structure based on the RTC

    timeinfo->tm_sec = (t_time >> 0) & ((1 << 5) - 1);	// 0..4
    timeinfo->tm_min = (t_time >> 5) & ((1 << 6) - 1);	// 5..10
    timeinfo->tm_hour = (t_time >> 11) & ((1 << 5) - 1);	// 11..15
    timeinfo->tm_mday = (t_date >> 0) & ((1 << 5) - 1);	// 0..4
    timeinfo->tm_mon = (t_date >> 5) & ((1 << 4) - 1);	// 5..8
    timeinfo->tm_year = (t_date >> 9) & ((1 << 7) - 1);	// 9..15

    // Convert to timestamp

    return timeinfo;
}


// trim from left
void ltrim(std::string& s, const char* t)
{
    s.erase(0, s.find_first_not_of(t));
}

