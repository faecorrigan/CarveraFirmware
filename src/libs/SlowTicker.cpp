#include "SlowTicker.h"
#include "Kernel.h"

void SlowTicker::on_module_loaded() {
    this->register_for_event(ON_IDLE);
}

void SlowTicker::start() {
    timer.start();
}

void SlowTicker::timerCallback() {
    xSemaphoreGive(semaphore);
}

void SlowTicker::on_idle(void*) {
    if (xSemaphoreTake(semaphore, 0) == pdTRUE) {
        THEKERNEL.call_event(ON_SECOND_TICK);
    }
}
