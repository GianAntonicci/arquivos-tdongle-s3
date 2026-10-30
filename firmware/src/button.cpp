#include "button.h"

#include <Arduino.h>

#include "config.h"

static bool     s_stable = false;      // estado estável (true = pressionado)
static bool     s_lastRaw = false;
static uint32_t s_rawSince = 0;
static uint32_t s_pressAt = 0;
static bool     s_longFired = false;
static bool     s_clickPending = false; // primeiro clique aguardando o segundo
static uint32_t s_clickAt = 0;
static volatile bool s_doubleEnabled = true;

namespace button {

void begin() { pinMode(PIN_BUTTON, INPUT_PULLUP); }

void setDoubleClickEnabled(bool en) {
    s_doubleEnabled = en;
    s_clickPending = false;
}

BtnEvent poll(uint32_t now) {
    bool raw = digitalRead(PIN_BUTTON) == LOW;

    // Debounce: o estado só muda depois de BTN_DEBOUNCE_MS estável
    if (raw != s_lastRaw) {
        s_lastRaw = raw;
        s_rawSince = now;
    }
    if (raw != s_stable && now - s_rawSince >= BTN_DEBOUNCE_MS) {
        s_stable = raw;
        if (s_stable) {
            s_pressAt = now;
            s_longFired = false;
        } else if (!s_longFired) {
            // Soltou antes do tempo de pressão longa: é um clique
            if (!s_doubleEnabled) return BtnEvent::Short;
            if (s_clickPending) {
                s_clickPending = false;
                return BtnEvent::Double;
            }
            s_clickPending = true;
            s_clickAt = now;
        }
    }

    // Pressão longa dispara ainda segurando, para dar retorno imediato
    if (s_stable && !s_longFired && now - s_pressAt >= BTN_LONG_MS) {
        s_longFired = true;
        s_clickPending = false;
        return BtnEvent::Long;
    }

    // Janela do clique duplo expirou: era um clique simples
    if (s_clickPending && !s_stable && now - s_clickAt > BTN_DOUBLE_MS) {
        s_clickPending = false;
        return BtnEvent::Short;
    }
    return BtnEvent::None;
}

}  // namespace button
