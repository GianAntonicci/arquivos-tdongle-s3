#pragma once
// Relógio e USB simulados apenas para os testes nativos.
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
inline uint32_t fakeNow = 0;
inline void (*fakeDelay)() = nullptr;
inline uint32_t millis() { return fakeNow; }
inline void delay(uint32_t ms) { fakeNow += ms; if (fakeDelay) fakeDelay(); }
using esp_event_base_t = const char *;
inline const char *ARDUINO_USB_EVENTS = "usb";
constexpr int ARDUINO_USB_STARTED_EVENT = 1;
constexpr int ARDUINO_USB_STOPPED_EVENT = 2;
