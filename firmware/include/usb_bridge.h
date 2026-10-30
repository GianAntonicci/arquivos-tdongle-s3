#pragma once
// USB nativa (TinyUSB): CDC para o protocolo + HID teclado no mesmo dispositivo.
//
// API de arquivo usada pelo portal Web. Tudo é streaming: nenhum byte de
// arquivo fica guardado no dongle além do bloco em trânsito. writeChunk()
// bloqueia até o PC confirmar (ACK); enquanto isso o portal não lê o socket,
// e o TCP segura o celular (contrapressão natural).
#include <stddef.h>
#include <stdint.h>

#include "app_state.h"

namespace usb_bridge {
void begin();             // fixa VID/PID/nomes e sobe a USB (depois de hid::begin)
void loop();              // processa HELLO/ACK quando não há upload
bool mounted();           // enumerado por um host
// true uma única vez após cada enumeração (para disparar o HID)
bool takeMountEvent();
bool hostAlive();         // página Web Serial mandando HELLO com destino pronto

ErrorReason beginFile(const char *name, uint64_t size, uint16_t index, uint16_t count);
ErrorReason writeChunk(const uint8_t *data, size_t len);
ErrorReason endFile();    // manda END e espera o PC confirmar (DONE)
void        abortFile(ErrorReason why);  // idempotente
}
