#ifndef _WATCHDOG_H
#define _WATCHDOG_H

#include <stdint.h>

#include "Module.h"

typedef enum
{
    WDT_MRI,
    WDT_RESET,
} WDT_ACTION;

class Watchdog : public Module {
public:
    Watchdog(uint32_t timeout, WDT_ACTION action) : timeout(timeout), action(action) {}
    
    void arm();
    void feed();

    void on_module_loaded();
    void on_idle(void*);

    void configure(uint32_t new_timeout, WDT_ACTION new_action) {
        timeout = new_timeout;
        action = new_action;
    }

private:
    uint32_t timeout;
    WDT_ACTION action;
};

#endif /* _WATCHDOG_H */
