// logging.h
#ifndef LOGGING_H
#define LOGGING_H

#include <cstdarg>

// The printk function
void printk(const char* format, ...) __attribute__ ((format(printf, 1, 2)));

#endif // LOGGING_H
