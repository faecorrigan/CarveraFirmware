// logging.cpp
#include "logging.h"
#include "libs/Kernel.h"

void printk(const char* format, ...) {
    va_list args;
    va_start(args, format);

    if (THEKERNEL != nullptr) {
        THEKERNEL->vprintk(format, args);
    }
    
    va_end(args);
}
