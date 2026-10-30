#pragma once
// USB nativa (TinyUSB): CDC para o protocolo + HID teclado no mesmo dispositivo.
#include <stdint.h>

namespace usb_bridge {
void begin();             // fixa VID/PID/nomes e sobe a USB (depois de hid::begin)
bool mounted();           // enumerado por um host
// true uma única vez após cada enumeração (para disparar o HID)
bool takeMountEvent();
bool hostAlive();         // página Web Serial mandando HELLO (etapa d)
}
