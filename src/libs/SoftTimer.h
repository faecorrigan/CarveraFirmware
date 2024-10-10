#pragma once

#include <assert.h>

#include "FreeRTOS.h"
#include "timers.h"

class SoftTimer {
public:
    /**
     * @brief Constructs a FreeRTOS SoftTimer.
     * 
     * This constructor initializes a FreeRTOS software timer The timer will call the 
     * specified member function on the provided object when it expires.
     * 
     * @tparam T The type of the object on which the member function will be called.
     * @param name The name of the timer (used for debugging purposes). Can be `nullptr`.
     * @param periodMs The timer period in milliseconds.
     * @param autoReload If `true`, the timer is periodic (auto-reloads after expiring).
     *                   If `false`, the timer is one-shot and stops after expiring.
     * @param object A pointer to the object instance (`this`) on which the member function will be called.
     * @param method A pointer to the member function to be called when the timer expires.
     * 
     * @note
     * - The `SoftTimer` class cannot be copied or moved to ensure the integrity of the `this` pointer
     *   used in the timer callback. Copy and move constructors and assignment operators are deleted.
     * - The timer callback runs in the context of the FreeRTOS Timer Service Task. Avoid blocking operations
     *   or lengthy computations in the callback to prevent delaying other timers.
     */
    template <typename T>
    SoftTimer(const char* name, uint32_t periodMs, bool autoReload, T* object, void (T::*method)())
        : timerHandle(nullptr), object_(static_cast<void*>(object)),
          memberFunction_(reinterpret_cast<MemberFunctionPointer>(method)) {

        timerHandle = xTimerCreateStatic(
            name,
            pdMS_TO_TICKS(periodMs),
            autoReload ? pdTRUE : pdFALSE,
            this,
            &SoftTimer::TimerCallback,
            &timerBuffer_
        );
    }

    ~SoftTimer() {
        xTimerDelete(timerHandle, 0);
    }

    // Delete copy constructor and copy assignment operator
    SoftTimer(const SoftTimer&) = delete;
    SoftTimer& operator=(const SoftTimer&) = delete;

    // Delete move constructor and move assignment operator
    SoftTimer(SoftTimer&&) = delete;
    SoftTimer& operator=(SoftTimer&&) = delete;

    void start() {
        xTimerStart(timerHandle, 0);
    }

    void stop() {
        xTimerStop(timerHandle, 0);
    }

    void reset() {
        xTimerReset(timerHandle, 0);
    }

    void setPeriod(uint32_t periodMs) {
        xTimerChangePeriod(timerHandle, pdMS_TO_TICKS(periodMs), 0);
    }

    void setFrequency(float frequencyHz) {
        assert(frequencyHz > 0.0f);

        uint32_t periodMs = static_cast<uint32_t>(1000.0f / frequencyHz);
        if (periodMs == 0) {
            periodMs = 1; // Minimum period of 1 ms to prevent zero period
        }
        setPeriod(periodMs);
    }

    bool isActive() const {
        return (timerHandle != nullptr) && (xTimerIsTimerActive(timerHandle) != pdFALSE);
    }

private:
    using MemberFunctionPointer = void (*)(void*);

    TimerHandle_t timerHandle;
    void* object_;
    MemberFunctionPointer memberFunction_;
    StaticTimer_t timerBuffer_; // Static buffer for the timer

    // Static callback function called by FreeRTOS
    static void TimerCallback(TimerHandle_t xTimer) {
        // Retrieve the instance of SoftTimer from the timer ID
        SoftTimer* timerInstance = static_cast<SoftTimer*>(pvTimerGetTimerID(xTimer));
        if (timerInstance && timerInstance->object_ && timerInstance->memberFunction_) {
            (timerInstance->memberFunction_)(timerInstance->object_);
        }
    }
};
