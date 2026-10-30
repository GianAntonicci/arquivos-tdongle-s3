// Mapas de caracteres -> teclas HID para os layouts US e ABNT2.
//
// Usage IDs referem-se à POSIÇÃO física da tecla (tabela HID 0x07). O SO do PC
// traduz a posição em caractere conforme o layout configurado nele. Por isso,
// para digitar "/" num PC configurado em ABNT2 é preciso mandar outra tecla.
//
// Teclado ABNT2 (diferenças em relação ao US):
//   0x35 (` do US)      -> ' "
//   0x23 (6)            -> 6 ¨ (shift = trema, tecla morta)
//   0x2F ([ do US)      -> ´ ` (tecla morta)
//   0x30 (] do US)      -> [ {
//   0x31 (\ do US)      -> ] }
//   0x33 (; do US)      -> ç Ç
//   0x34 (' do US)      -> ~ ^ (tecla morta)
//   0x64 (ISO, ao lado do Z) -> \ |
//   0x38 (/ do US)      -> ; :
//   0x87 (ABNT2, ao lado do shift direito) -> / ?
#include "keymap.h"

static const uint8_t SHIFT = HID_MOD_LSHIFT;

// Letras, números, espaço e controles são iguais nos dois layouts
static bool common(char c, KeyStroke &k) {
    k.mod = 0;
    k.dead = false;
    if (c >= 'a' && c <= 'z') { k.key = hidLetter(c); return true; }
    if (c >= 'A' && c <= 'Z') { k.key = hidLetter(c); k.mod = SHIFT; return true; }
    if (c >= '1' && c <= '9') { k.key = 0x1E + (c - '1'); return true; }
    switch (c) {
        case '0':  k.key = 0x27; return true;
        case ' ':  k.key = HID_KEY_SPACE; return true;
        case '\n': k.key = HID_KEY_ENTER; return true;
        case '\t': k.key = HID_KEY_TAB; return true;
        // mesma posição e mesmo shift nos dois layouts
        case '!':  k.key = 0x1E; k.mod = SHIFT; return true;
        case '@':  k.key = 0x1F; k.mod = SHIFT; return true;
        case '#':  k.key = 0x20; k.mod = SHIFT; return true;
        case '$':  k.key = 0x21; k.mod = SHIFT; return true;
        case '%':  k.key = 0x22; k.mod = SHIFT; return true;
        case '&':  k.key = 0x24; k.mod = SHIFT; return true;
        case '*':  k.key = 0x25; k.mod = SHIFT; return true;
        case '(':  k.key = 0x26; k.mod = SHIFT; return true;
        case ')':  k.key = 0x27; k.mod = SHIFT; return true;
        case '-':  k.key = 0x2D; return true;
        case '_':  k.key = 0x2D; k.mod = SHIFT; return true;
        case '=':  k.key = 0x2E; return true;
        case '+':  k.key = 0x2E; k.mod = SHIFT; return true;
        case ',':  k.key = 0x36; return true;
        case '<':  k.key = 0x36; k.mod = SHIFT; return true;
        case '.':  k.key = 0x37; return true;
        case '>':  k.key = 0x37; k.mod = SHIFT; return true;
        // "/" pelo teclado numérico (0x54) nos dois layouts: sai "/" em qualquer
        // layout e SO, inclusive no macOS, que nem sempre reconhece a tecla 0x87
        // do ABNT2. É o que deixa a URL independente do layout.
        case '/':  k.key = 0x54; return true;
        default:   return false;
    }
}

static bool mapUS(char c, KeyStroke &k) {
    switch (c) {
        case '^':  k.key = 0x23; k.mod = SHIFT; return true;
        case '[':  k.key = 0x2F; return true;
        case '{':  k.key = 0x2F; k.mod = SHIFT; return true;
        case ']':  k.key = 0x30; return true;
        case '}':  k.key = 0x30; k.mod = SHIFT; return true;
        case '\\': k.key = 0x31; return true;
        case '|':  k.key = 0x31; k.mod = SHIFT; return true;
        case ';':  k.key = 0x33; return true;
        case ':':  k.key = 0x33; k.mod = SHIFT; return true;
        case '\'': k.key = 0x34; return true;
        case '"':  k.key = 0x34; k.mod = SHIFT; return true;
        case '`':  k.key = 0x35; return true;
        case '~':  k.key = 0x35; k.mod = SHIFT; return true;
        case '?':  k.key = 0x38; k.mod = SHIFT; return true;
        default:   return false;
    }
}

static bool mapABNT2(char c, KeyStroke &k) {
    switch (c) {
        case '\'': k.key = 0x35; return true;
        case '"':  k.key = 0x35; k.mod = SHIFT; return true;
        case '[':  k.key = 0x30; return true;
        case '{':  k.key = 0x30; k.mod = SHIFT; return true;
        case ']':  k.key = 0x31; return true;
        case '}':  k.key = 0x31; k.mod = SHIFT; return true;
        case '\\': k.key = 0x64; return true;
        case '|':  k.key = 0x64; k.mod = SHIFT; return true;
        case ';':  k.key = 0x38; return true;
        case ':':  k.key = 0x38; k.mod = SHIFT; return true;
        case '?':  k.key = 0x87; k.mod = SHIFT; return true;
        // Teclas mortas: o caractere só sai depois de um Espaço
        case '~':  k.key = 0x34; k.dead = true; return true;
        case '^':  k.key = 0x34; k.mod = SHIFT; k.dead = true; return true;
        case '`':  k.key = 0x2F; k.mod = SHIFT; k.dead = true; return true;
        default:   return false;
    }
}

bool keymapLookup(KbLayout layout, char c, KeyStroke &out) {
    if (common(c, out)) return true;
    out.mod = 0;
    out.dead = false;
    return layout == KbLayout::ABNT2 ? mapABNT2(c, out) : mapUS(c, out);
}
