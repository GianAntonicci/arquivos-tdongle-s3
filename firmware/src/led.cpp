#include "led.h"

#include <Arduino.h>

#include "config.h"

// Driver APA102 mínimo por bit-bang: com um único LED não vale carregar o FastLED.
// Quadro: 32 bits 0 (início) | 0b111 + brilho 5 bits, B, G, R | 32 bits 1 (fim)

static volatile LedPattern s_pattern = LedPattern::Off;
static volatile uint32_t   s_flashUntil = 0;
static volatile uint8_t    s_global = 2;   // brilho global APA102 (0..31)
static uint32_t            s_lastWord = 0xFFFFFFFF;

// Brilho global para cada nível do menu (1..5)
static const uint8_t LEVEL_TO_GLOBAL[6] = {0, 1, 2, 4, 8, 16};

static void shiftByte(uint8_t b) {
    for (int i = 7; i >= 0; --i) {
        digitalWrite(PIN_LED_DATA, (b >> i) & 1);
        digitalWrite(PIN_LED_CLK, HIGH);
        digitalWrite(PIN_LED_CLK, LOW);
    }
}

static void writeLed(uint8_t r, uint8_t g, uint8_t b) {
    uint8_t gb = (r | g | b) ? s_global : 0;
    uint32_t word = ((uint32_t)gb << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
    if (word == s_lastWord) return;  // só transmite quando a cor muda
    s_lastWord = word;
    for (int i = 0; i < 4; ++i) shiftByte(0x00);
    shiftByte(0xE0 | gb);
    shiftByte(b);
    shiftByte(g);
    shiftByte(r);
    for (int i = 0; i < 4; ++i) shiftByte(0xFF);
}

// Onda triangular 0..255 com período dado
static uint8_t triangle(uint32_t now, uint32_t period) {
    uint32_t ph = now % period;
    uint32_t half = period / 2;
    uint32_t v = ph < half ? ph : period - ph;
    return (uint8_t)(v * 255 / half);
}

namespace led {

void begin() {
    pinMode(PIN_LED_DATA, OUTPUT);
    pinMode(PIN_LED_CLK, OUTPUT);
    digitalWrite(PIN_LED_CLK, LOW);
    writeLed(0, 0, 0);
}

void setPattern(LedPattern p) { s_pattern = p; }

void flashSuccess() { s_flashUntil = millis() + 600; }

void setLevel(uint8_t level) {
    if (level < 1) level = 1;
    if (level > 5) level = 5;
    s_global = LEVEL_TO_GLOBAL[level];
    s_lastWord = 0xFFFFFFFF;  // força retransmissão
}

void update(uint32_t now) {
    // Flash de sucesso sobrepõe o padrão: 3 piscadas verdes
    if ((int32_t)(s_flashUntil - now) > 0) {
        bool on = ((s_flashUntil - now) / 100) % 2 == 0;
        writeLed(0, on ? 255 : 0, 0);
        return;
    }

    switch (s_pattern) {
        case LedPattern::Off:
            writeLed(0, 0, 0);
            break;
        case LedPattern::WaitingHost: {
            uint8_t v = triangle(now, 2000);
            writeLed(0, 0, 20 + (uint16_t)v * 235 / 255);
            break;
        }
        case LedPattern::Ready:
            writeLed(0, 60, 0);  // verde baixo
            break;
        case LedPattern::Transfer: {
            bool on = (now / 150) % 2 == 0;
            writeLed(0, on ? 200 : 0, on ? 200 : 0);
            break;
        }
        case LedPattern::Error:
            writeLed(255, 0, 0);
            break;
        case LedPattern::Hid:
            writeLed(160, 0, 255);
            break;
    }
}

}  // namespace led
