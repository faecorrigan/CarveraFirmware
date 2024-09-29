/*
      This file is part of Smoothie (http://smoothieware.org/). The motion control part is heavily based on Grbl (https://github.com/simen/grbl).
      Smoothie is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
      Smoothie is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
      You should have received a copy of the GNU General Public License along with Smoothie. If not, see <http://www.gnu.org/licenses/>.
*/


#include "Gcode.h"
#include "libs/StreamOutput.h"
#include "utils.h"
#include <stdlib.h>
#include <algorithm>


//required for system variables
#include <cctype>
#include "libs/Kernel.h"
#include "Robot.h"
#include "PublicData.h"
#include "SpindlePublicAccess.h"
#include "StepperMotor.h"

// This is a gcode object. It represents a GCode string/command, and caches some important values about that command for the sake of performance.
// It gets passed around in events, and attached to the queue ( that'll change )
Gcode::Gcode(const string &command, StreamOutput *stream, bool strip, unsigned int line)
{
    this->command= strdup(command.c_str());
    this->m= 0;
    this->g= 0;
    this->subcode= 0;
    this->add_nl= false;
    this->is_error= false;
    this->stream= stream;
    prepare_cached_values(strip);
    this->stripped= strip;
    this->line = line;
}

Gcode::~Gcode()
{
    if(command != nullptr) {
        // TODO we can reference count this so we share copies, may save more ram than the extra count we need to store
        free(command);
    }
}

Gcode::Gcode(const Gcode &to_copy)
{
    this->command               = strdup(to_copy.command); // TODO we can reference count this so we share copies, may save more ram than the extra count we need to store
    this->has_m                 = to_copy.has_m;
    this->has_g                 = to_copy.has_g;
    this->m                     = to_copy.m;
    this->g                     = to_copy.g;
    this->subcode               = to_copy.subcode;
    this->add_nl                = to_copy.add_nl;
    this->is_error              = to_copy.is_error;
    this->stream                = to_copy.stream;
    this->txt_after_ok.assign( to_copy.txt_after_ok );
}

Gcode &Gcode::operator= (const Gcode &to_copy)
{
    if( this != &to_copy ) {
        this->command               = strdup(to_copy.command); // TODO we can reference count this so we share copies, may save more ram than the extra count we need to store
        this->has_m                 = to_copy.has_m;
        this->has_g                 = to_copy.has_g;
        this->m                     = to_copy.m;
        this->g                     = to_copy.g;
        this->subcode               = to_copy.subcode;
        this->add_nl                = to_copy.add_nl;
        this->is_error              = to_copy.is_error;
        this->stream                = to_copy.stream;
        this->txt_after_ok.assign( to_copy.txt_after_ok );
    }
    return *this;
}


// Whether or not a Gcode has a letter
bool Gcode::has_letter( char letter ) const
{
    for (size_t i = 0; i < strlen(this->command); ++i) {
        if( command[i] == letter ) {
            return true;
        }
    }
    return false;
}

//2024
/*
int Gcode::index_of_letter( char letter, int start ) const
{
    for (size_t i = start; i < strlen(this->command); ++i) {
        if( command[i] == letter ) {
            return i;
        }
    }
    return -1;
}
*/

float Gcode::set_variable_value() const{
    // Expecting a number after the `#` from 1-20, like #12
    const char* expr = this->get_command();
    if (*expr == '#') {
        char* endptr;
        float value = 0;
        int var_num = strtol(expr + 1, &endptr, 10); 
        
        while (*endptr == ' ')
        {
            endptr++;
        }

        if (*endptr == '=')
        {
            endptr++;
            while (*endptr == ' ') //skip whitespace
            {
                endptr++;
            }
            value = evaluate_expression(endptr, &endptr);
        }
        else
        {  
            const char* temp_expr = expr;  // Temporary variable for safe parsing
            value = this->get_variable_value(expr, (char**)&temp_expr);
            
            if (value > -100000){
                this->stream->printf("variable %d = %.4f \n", var_num , value);
            }
            else
            {
                this->stream->printf("variable %d not set \n", var_num);
                THEKERNEL.call_event(ON_HALT, nullptr);
                THEKERNEL.set_halt_reason(MANUAL);
                return 0;
                
            }
            return 0;
        }

        if (var_num >= 101 && var_num <= 120) {
            THEKERNEL.local_vars[var_num -101] = value;
            this->stream->printf("Variable %d set %.4f \n", var_num,value);
            return value;
        } else if(var_num >= 501 && var_num <= 520)
        {
            THEKERNEL.eeprom_data.perm_vars[var_num - 501] = value;
            THEKERNEL.write_eeprom_data();
            this->stream->printf("Variable %d set  %.4f \n", var_num , value);
            return value;
        }else //system variables
        {
        }
    }
    this->stream->printf("Variable not found \n");
    return 0;
    
}


//get the value of a particular variable stored in EEPROM
float Gcode::get_variable_value(const char* expr, char** endptr) const{
    // Expecting a number after the `#` from 1-20, like #12
    if (*expr == '#') {
        int var_num = strtol(expr + 1, endptr, 10);         
        if (var_num >= 101 && var_num <= 120) {
            if (THEKERNEL.local_vars[var_num -101] > -100000)
            {
                return THEKERNEL.local_vars[var_num -101];
            }
            this->stream->printf("Variable %d not set \n", var_num);
            THEKERNEL.call_event(ON_HALT, nullptr);
            THEKERNEL.set_halt_reason(MANUAL);
            return 0;
        } else if(var_num >= 501 && var_num <= 520)
        {
            if (THEKERNEL.eeprom_data.perm_vars[var_num - 501] > -100000)
            {
                return THEKERNEL.eeprom_data.perm_vars[var_num - 501]; // return permanent variables
            }
            this->stream->printf("Variable %d not set \n", var_num);
            THEKERNEL.call_event(ON_HALT, nullptr);
            THEKERNEL.set_halt_reason(MANUAL);
            return 0;
        }else //system variables
        {
            float mpos[3];
            bool ok;
            wcs_t pos;
            switch (var_num){
                case 2000: //stored tool length offset
                    return THEKERNEL.eeprom_data.TLO;
                    break;
                case 2500: //root WCS x position in relation to machine 0
                    return 0;
                    break;
                case 2600: //root WCS y position in relation to machine 0
                    return 0;
                    break;
                case 2700: //root WCS z position in relation to machine 0
                    return 0;
                    break;
                case 2800: //root WCS a position in relation to machine 0
                    return 0;
                    break;
                case 2501: //root G54 WCS x position in relation to machine 0
                    return 0;
                    break;
                case 2601: //root G54 WCS y position in relation to machine 0
                    return 0;
                    break;
                case 2701: //root G54 WCS z position in relation to machine 0
                    return 0;
                    break;
                case 2801: //root G54 WCS a position in relation to machine 0
                    return 0;
                    break;
                //add rest of WCS eventually

                case 3026: //tool in spindle
                    return THEKERNEL.eeprom_data.TOOL;
                    break;
                case 3027: //current spindle RPM
                    struct spindle_status ss;
                    ok = PublicData::get_value(pwm_spindle_control_checksum, get_spindle_status_checksum, &ss);
                    if (ok) {
                        return ss.current_rpm;
                        break;
                    }
                    return 0;
                    break;
                case 3033: //Op Stop Enabled
                    return THEKERNEL.get_optional_stop_mode();
                    break;
                case 5021: //current machine X position
                    THEROBOT.get_current_machine_position(mpos);
                    // current_position/mpos includes the compensation transform so we need to get the inverse to get actual position
                    if(THEROBOT.compensationTransform) THEROBOT.compensationTransform(mpos, true, false); // get inverse compensation transform
                    return mpos[X_AXIS];
                    break;
                case 5022: //current machine Y position
                    THEROBOT.get_current_machine_position(mpos);
                    // current_position/mpos includes the compensation transform so we need to get the inverse to get actual position
                    if(THEROBOT.compensationTransform) THEROBOT.compensationTransform(mpos, true, false); // get inverse compensation transform
                    return mpos[Y_AXIS];
                    break;
                case 5023: //current machine Z position
                    THEROBOT.get_current_machine_position(mpos);
                    // current_position/mpos includes the compensation transform so we need to get the inverse to get actual position
                    if(THEROBOT.compensationTransform) THEROBOT.compensationTransform(mpos, true, false); // get inverse compensation transform
                    return mpos[Z_AXIS];
                    break;

                #if MAX_ROBOT_ACTUATORS > 3
                case 5024: //current machine A position
                    return THEROBOT.actuators[A_AXIS]->get_current_position();
                    break;
                #endif
                case 5041: //current WCS X position
                     THEROBOT.get_current_machine_position(mpos);
                    // current_position/mpos includes the compensation transform so we need to get the inverse to get actual position
                    if(THEROBOT.compensationTransform) THEROBOT.compensationTransform(mpos, true, false); // get inverse compensation transform
                    pos= THEROBOT.mcs2wcs(mpos);
                    return THEROBOT.from_millimeters(std::get<X_AXIS>(pos));
                    return 0;
                    break;
                case 5042: //current WCS Y position
                     THEROBOT.get_current_machine_position(mpos);
                    // current_position/mpos includes the compensation transform so we need to get the inverse to get actual position
                    if(THEROBOT.compensationTransform) THEROBOT.compensationTransform(mpos, true, false); // get inverse compensation transform
                    pos= THEROBOT.mcs2wcs(mpos);
                    return THEROBOT.from_millimeters(std::get<Y_AXIS>(pos));
                    return 0;
                    break;
                case 5043: //current WCS A position
                     THEROBOT.get_current_machine_position(mpos);
                    // current_position/mpos includes the compensation transform so we need to get the inverse to get actual position
                    if(THEROBOT.compensationTransform) THEROBOT.compensationTransform(mpos, true, false); // get inverse compensation transform
                    pos= THEROBOT.mcs2wcs(mpos);
                    return THEROBOT.from_millimeters(std::get<Z_AXIS>(pos));
                    return 0;
                    break;
                #if MAX_ROBOT_ACTUATORS > 3
                case 5044: //current machine A position
                    return THEROBOT.actuators[A_AXIS]->get_current_position();
                    break;
                #endif

                default:
                    this->stream->printf("Variable %d not found \n", var_num);
                    THEKERNEL.call_event(ON_HALT, nullptr);
                    THEKERNEL.set_halt_reason(MANUAL);
                    return 0;
                    break;
            }
        }
    }
    return 0;
}

// Evaluate gcode values containing math and variable calls
float Gcode::evaluate_expression(const char* expr, char** endptr) const {
    while (isspace(*expr)) expr++;  // Skip leading whitespace

    float result;
    if (*expr == '#') {  // if the line starts with #, get variable value
        result = this->get_variable_value(expr, endptr);
    } else {
        result = strtof(expr, endptr);
    }

    while (*endptr && **endptr) {
        // Skip any whitespace between numbers/operators
        while (isspace(**endptr)) (*endptr)++;
							
					 

        char op = **endptr;  // Get the operator
        (*endptr)++;         // Move past the operator

        // Skip any whitespace after the operator
        while (isspace(**endptr)) (*endptr)++;

        float next_val;
        if (**endptr == '#') {
            next_val = this->get_variable_value(*endptr, endptr);
        } else {
            // Handle negative numbers or numbers with a leading sign
            next_val = strtof(*endptr, endptr);
        }

        // Perform the operation
        switch (op) {
            case '+':
                result += next_val;
                break;
            case '-':
                result -= next_val;
                break;
            case '*':
                result *= next_val;
                break;
            case '/':
                if (next_val != 0)  // Avoid division by zero
                    result /= next_val;
                break;
					 
					  
            default:
                // If it's an unrecognized operator, stop parsing
                return result;
        }
		
    }
    return result;
}


// Retrieve the value for a given letter
float Gcode::get_value( char letter, char **ptr ) const
{
    const char *cs = command;
    char *cn = NULL;
    for (; *cs; cs++) {
        if (letter == *cs) {
            cs++;
            float result = this->evaluate_expression(cs, &cn);
            if(ptr != nullptr) *ptr = cn;
            
            // If a valid expression was found, return the result
            if (cn > cs)
                return result;
        }
    }
    // If no valid number or expression is found, return 0
    if (ptr != nullptr) *ptr = nullptr;
    return 0;
}

// 2024
/*
// Retrieve the value for a given letter
float Gcode::get_value_at_index( int index ) const
{
    const char *cs = command + index + 1;
    char *cn = NULL;
	float r = strtof(cs, &cn);
	if(cn > cs)
		return r;

    return 0;
}*/

int Gcode::get_int( char letter, char **ptr ) const
{
    const char *cs = command;
    char *cn = NULL;
    for (; *cs; cs++) {
        if( letter == *cs ) {
            cs++;
            int r = strtol(cs, &cn, 10);
            if(ptr != nullptr) *ptr= cn;
            if (cn > cs)
                return r;
        }
    }
    if(ptr != nullptr) *ptr= nullptr;
    return 0;
}

uint32_t Gcode::get_uint( char letter, char **ptr ) const
{
    const char *cs = command;
    char *cn = NULL;
    for (; *cs; cs++) {
        if( letter == *cs ) {
            cs++;
            int r = strtoul(cs, &cn, 10);
            if(ptr != nullptr) *ptr= cn;
            if (cn > cs)
                return r;
        }
    }
    if(ptr != nullptr) *ptr= nullptr;
    return 0;
}

int Gcode::get_num_args() const
{
    int count = 0;
    for(size_t i = stripped?0:1; i < strlen(command); i++) {
        if( this->command[i] >= 'A' && this->command[i] <= 'Z' ) {
            if(this->command[i] == 'T') continue;
            count++;
        }
    }
    return count;
}

std::map<char,float> Gcode::get_args() const
{
    std::map<char,float> m;
    for(size_t i = stripped?0:1; i < strlen(command); i++) {
        char c= this->command[i];
        if( c >= 'A' && c <= 'Z' ) {
            if(c == 'T') continue;
            m[c]= get_value(c);
        }
    }
    return m;
}

std::map<char,int> Gcode::get_args_int() const
{
    std::map<char,int> m;
    for(size_t i = stripped?0:1; i < strlen(command); i++) {
        char c= this->command[i];
        if( c >= 'A' && c <= 'Z' ) {
            if(c == 'T') continue;
            m[c]= get_int(c);
        }
    }
    return m;
}

// Cache some of this command's properties, so we don't have to parse the string every time we want to look at them
void Gcode::prepare_cached_values(bool strip)
{
    char *p= nullptr;

    if( this->has_letter('G') ) {
        this->has_g = true;
        this->g = this->get_int('G', &p);

    } else {
        this->has_g = false;
    }

    if( this->has_letter('M') ) {
        this->has_m = true;
        this->m = this->get_int('M', &p);

    } else {
        this->has_m = false;
    }


    if(has_g || has_m) {
        // look for subcode and extract it
        if(p != nullptr && *p == '.') {
            this->subcode = strtoul(p+1, &p, 10);

        }else{
            this->subcode= 0;
        }
    }

    if(!strip || this->has_letter('T')) return;

    // remove the Gxxx or Mxxx from string
    if (p != nullptr) {
        char *n= strdup(p); // create new string starting at end of the numeric value
        free(command);
        command= n;
    }
}

// strip off X Y Z I J K parameters if G0/1/2/3
void Gcode::strip_parameters()
{
    if(has_g && g < 4){
        // strip the command of the XYZIJK parameters
        string newcmd;
        char *cn= command;
        // find the start of each parameter
        char *pch= strpbrk(cn, "XYZIJK");
        while (pch != nullptr) {
            if(pch > cn) {
                // copy non parameters to new string
                newcmd.append(cn, pch-cn);
            }
            // find the end of the parameter and its value
            char *eos;
            strtof(pch+1, &eos);
            cn= eos; // point to end of last parameter
            pch= strpbrk(cn, "XYZIJK"); // find next parameter
        }
        // append anything left on the line
        newcmd.append(cn);

        // strip whitespace to save even more, this causes problems so don't do it
        //newcmd.erase(std::remove_if(newcmd.begin(), newcmd.end(), ::isspace), newcmd.end());

        // release the old one
        free(command);
        // copy the new shortened one
        command= strdup(newcmd.c_str());
    }
}
