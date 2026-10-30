#pragma once
// Conversão de caracteres ASCII em teclas HID (usage IDs) por layout.
// A biblioteca HID do Arduino assume teclado US; para ABNT2 a mesma tecla
// física gera outro caractere, então o mapa é próprio (keymap_abnt2.cpp).
#include <stdint.h>

#include "app_state.h"

// Modificadores HID (byte de modificadores do relatório de teclado)
#define HID_MOD_LCTRL  0x01
#define HID_MOD_LSHIFT 0x02
#define HID_MOD_LALT   0x04
#define HID_MOD_LGUI   0x08
#define HID_MOD_RALT   0x40  // AltGr

// Usage IDs usados fora do mapa de caracteres
#define HID_KEY_ENTER 0x28
#define HID_KEY_ESC   0x29
#define HID_KEY_TAB   0x2B
#define HID_KEY_SPACE 0x2C
#define HID_KEY_F2    0x3B
#define HID_KEY_A     0x04

static inline uint8_t hidLetter(char c) { return HID_KEY_A + (c | 0x20) - 'a'; }

struct KeyStroke {
    uint8_t mod;   // modificadores
    uint8_t key;   // usage ID
    bool    dead;  // tecla morta (~ ^ ` no ABNT2): precisa de Espaço depois
};

// Devolve false se o caractere não tem tecla no layout
bool keymapLookup(KbLayout layout, char c, KeyStroke &out);
