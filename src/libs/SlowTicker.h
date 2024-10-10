#ifndef SLOWTICKER_H
#define SLOWTICKER_H

#include "Module.h"
#include "SoftTimer.h"
#include "FreeRTOS.h"
#include "semphr.h"

class SlowTicker : public Module {
    public:
        SlowTicker()
        : timer("SlowTickerTimer", 1000, true, this, &SlowTicker::timerCallback)
        {
            semaphore = xSemaphoreCreateCounting(1, 0);
        }

        void on_module_loaded(void);
        void on_idle(void*);
        void start();

    private:
        SoftTimer timer;
        SemaphoreHandle_t semaphore;

        void timerCallback();
};

#endif
