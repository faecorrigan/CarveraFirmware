#include "StreamOutput.h"

NullStreamOutput StreamOutput::NullStream;

int StreamOutput::printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    
    // Call vprintf, which already handles the buffer allocation and formatting
    int result = vprintf(format, args);
    
    va_end(args);
    return result;
}

int StreamOutput::vprintf(const char *format, va_list args)
{
    char b[64];
    char *buffer;
    
    // Determine the required buffer size
    int size = vsnprintf(b, sizeof(b), format, args) + 1; // +1 for the terminating \0

    if (size <= static_cast<int>(sizeof(b))) {
        buffer = b;
    } else {
        buffer = new char[size];
        vsnprintf(buffer, size, format, args);
    }

    // Output the formatted string
    puts(buffer, strlen(buffer));

    // Clean up if dynamic memory was used
    if (buffer != b) {
        delete[] buffer;
    }

    return size - 1;
}
