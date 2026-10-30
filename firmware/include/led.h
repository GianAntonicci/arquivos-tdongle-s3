#pragma once
// LED RGB APA102 (1 LED). Padrões animados atualizados por led::update().
#include <stdint.h>

enum class LedPattern : uint8_t {
    Off,
    WaitingHost,  // azul pulsando
    Ready,        // verde fixo baixo
    Transfer,     // ciano piscando
    Error,        // vermelho
    Hid,          // roxo
};

namespace led {
void begin();
void setPattern(LedPattern p);
void flashSuccess();          // verde piscando por ~600 ms, depois volta ao padrão
void setLevel(uint8_t level); // 1..5
void update(uint32_t now);    // chamar periodicamente (task de UI)
}
