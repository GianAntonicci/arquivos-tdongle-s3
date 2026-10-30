#pragma once
// Eventos que os módulos (portal, ponte USB) avisam ao app (main.cpp),
// que cuida de tela e LED.
#include <stdint.h>

#include "app_state.h"

namespace app {
void transferStarted(const char *name, uint64_t size, uint16_t index, uint16_t count);
void transferProgress(uint64_t done, uint32_t bytesPerSec);
void transferFinished(ErrorReason result);   // None = sucesso
void stationsChanged(uint8_t count, bool joined);
}
