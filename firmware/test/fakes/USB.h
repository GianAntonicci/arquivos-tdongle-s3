#pragma once
#include "Arduino.h"
struct FakeUSB {
    void VID(uint16_t) {}
    void PID(uint16_t) {}
    void manufacturerName(const char *) {}
    void productName(const char *) {}
    void onEvent(void (*)(void *, esp_event_base_t, int32_t, void *)) {}
    void begin() {}
};
inline FakeUSB USB;
