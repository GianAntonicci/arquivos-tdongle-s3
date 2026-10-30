#include "hid_launcher.h"

#include <Arduino.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <string.h>

#include "config.h"
#include "display_ui.h"
#include "host_os.h"
#include "keymap.h"
#include "settings.h"

// Registrado na construção global: precisa existir antes de USB.begin()
static USBHIDKeyboard s_kbd;

// ---------- Roteiro de passos ----------
enum class StepType : uint8_t { Combo, Text, Delay };

struct Step {
    StepType    type;
    uint8_t     mod;
    uint8_t     key;
    uint16_t    ms;
    const char *text;
};

static const uint8_t MAX_STEPS = 12;
static Step     s_steps[MAX_STEPS];
static uint8_t  s_count = 0;
static uint8_t  s_idx = 0;
static char     s_text[320];      // texto a digitar (comando + URL)
static char     s_text2[160];     // segundo texto (macOS: URL)

static HidState s_state = HidState::Idle;
static uint32_t s_nextAt = 0;
static size_t   s_pos = 0;        // posição no texto atual
static uint8_t  s_phase = 0;      // 0 apertar, 1 soltar, 2 espaço (tecla morta), 3 soltar espaço
static KeyStroke s_cur;
static volatile bool s_capsLock = false;
static bool s_launchPending = false;

static void addCombo(uint8_t mod, uint8_t key) { s_steps[s_count++] = {StepType::Combo, mod, key, 0, nullptr}; }
static void addText(const char *t) { s_steps[s_count++] = {StepType::Text, 0, 0, 0, t}; }
static void addDelay(uint16_t ms) { s_steps[s_count++] = {StepType::Delay, 0, 0, ms, nullptr}; }

static void sendKeys(uint8_t mod, uint8_t key) {
    KeyReport r = {};
    r.modifiers = mod;
    r.keys[0] = key;
    s_kbd.sendReport(&r);
}

static void releaseAll() {
    KeyReport r = {};
    s_kbd.sendReport(&r);
}

// Copia `src` para `dst` trocando %URL% pela URL configurada
static void expandUrl(char *dst, size_t cap, const char *src, const char *url) {
    size_t o = 0;
    while (*src && o + 1 < cap) {
        if (strncmp(src, "%URL%", 5) == 0) {
            for (const char *u = url; *u && o + 1 < cap; ++u) dst[o++] = *u;
            src += 5;
        } else {
            dst[o++] = *src++;
        }
    }
    dst[o] = '\0';
}

// URL como será digitada: sem "https://". O ":" muda de tecla entre US e ABNT2;
// sem ele a URL padrão só usa letras, "." "-" e "/" (teclado numérico), que saem
// iguais em qualquer layout. O Chrome completa o esquema sozinho e o GitHub
// Pages redireciona para HTTPS.
static const char *typedUrl(const char *url) {
    return strncmp(url, "https://", 8) == 0 ? url + 8 : url;
}

// Monta o roteiro conforme o SO alvo
static bool buildScript() {
    const Settings &s = settings::get();
    const char *url = typedUrl(s.pcUrl);
    s_count = 0;
    switch (host_os::effective()) {
        case TargetOS::Windows:
            // Win+R -> "chrome <URL>" -> Enter
            snprintf(s_text, sizeof(s_text), "chrome %s", url);
            addCombo(HID_MOD_LGUI, hidLetter('r'));
            addDelay(HID_DIALOG_DELAY_MS);
            addText(s_text);
            addCombo(0, HID_KEY_ENTER);
            break;

        case TargetOS::MacOS:
            // Cmd+Espaço (Spotlight) -> "Google Chrome" -> Enter -> espera ->
            // Cmd+T (aba nova, foco na barra de endereço) -> URL -> Enter.
            // Cmd+T em vez de Cmd+L para não substituir a aba que o usuário já tinha aberta.
            strncpy(s_text, "Google Chrome", sizeof(s_text));
            strncpy(s_text2, url, sizeof(s_text2) - 1);
            s_text2[sizeof(s_text2) - 1] = '\0';
            addCombo(HID_MOD_LGUI, HID_KEY_SPACE);
            addDelay(HID_DIALOG_DELAY_MS);
            addText(s_text);
            // O Spotlight busca de forma assíncrona e ignora o Enter enquanto o
            // resultado principal ainda não apareceu; com pouca espera o Enter se
            // perde e a URL acaba digitada no próprio Spotlight
            addDelay(HID_SPOTLIGHT_SEARCH_MS);
            addCombo(0, HID_KEY_ENTER);
            addDelay(HID_APP_OPEN_DELAY_MS);
            addCombo(HID_MOD_LGUI, hidLetter('t'));
            addDelay(400);
            addText(s_text2);
            addCombo(0, HID_KEY_ENTER);
            break;

        case TargetOS::Linux:
        {
            char cmd[256];
            expandUrl(cmd, sizeof(cmd), s.linuxCmd, url);
            if (s.linuxMode == 1) {
                // Ctrl+Alt+T abre um terminal; o Chrome é desacoplado e o terminal fecha.
                // "&&" em vez de ";": o ";" muda de tecla entre US e ABNT2, o "&" não.
                snprintf(s_text, sizeof(s_text), "setsid -f %s >/dev/null 2>&1 && exit", cmd);
                addCombo(HID_MOD_LCTRL | HID_MOD_LALT, hidLetter('t'));
                addDelay(1500);
            } else {
                // Alt+F2: "executar comando" do GNOME/KDE/XFCE
                strncpy(s_text, cmd, sizeof(s_text) - 1);
                s_text[sizeof(s_text) - 1] = '\0';
                addCombo(HID_MOD_LALT, HID_KEY_F2);
                addDelay(HID_DIALOG_DELAY_MS);
            }
            addText(s_text);
            addCombo(0, HID_KEY_ENTER);
            break;
        }
        default:
            return false;
    }
    return true;
}

static void onKbdEvent(void *, esp_event_base_t, int32_t id, void *data) {
    if (id == ARDUINO_USB_HID_KEYBOARD_LED_EVENT) {
        auto *d = (arduino_usb_hid_keyboard_event_data_t *)data;
        s_capsLock = d->capslock;
    }
}

namespace hid {

void begin() {
    s_kbd.onEvent(onKbdEvent);
    s_kbd.begin();
}

void requestLaunch(uint32_t delayMs) {
    if (s_state != HidState::Idle) return;
    // Só escolhe o roteiro ao terminar a espera: as requisições STRING podem
    // chegar depois do evento de montagem da USB.
    s_launchPending = true;
    s_idx = 0;
    s_pos = 0;
    s_phase = 0;
    s_nextAt = millis() + delayMs;
    s_state = delayMs ? HidState::Waiting : HidState::Running;
}

void requestType(const char *text) {
    if (s_state != HidState::Idle) return;
    strncpy(s_text, text, sizeof(s_text) - 1);
    s_text[sizeof(s_text) - 1] = '\0';
    s_count = 0;
    s_launchPending = false;
    addText(s_text);
    s_idx = 0;
    s_pos = 0;
    s_phase = 0;
    s_nextAt = millis();
    s_state = HidState::Running;
}

void cancel() {
    if (s_state == HidState::Running) releaseAll();
    s_state = HidState::Idle;
    s_launchPending = false;
}

HidState state() { return s_state; }

void tick(uint32_t now) {
    if (s_state == HidState::Idle || (int32_t)(now - s_nextAt) < 0) return;
    if (s_launchPending) {
        s_launchPending = false;
        if (!buildScript()) {
            s_state = HidState::Idle;
            UiLock ui;
            snprintf(ui->toast, sizeof(ui->toast), "Escolha o SO no menu");
            ui->toastUntil = now + UI_TOAST_MS;
            return;
        }
    }
    s_state = HidState::Running;

    if (s_idx >= s_count) {
        releaseAll();
        s_state = HidState::Idle;
        return;
    }

    const Step &st = s_steps[s_idx];
    const uint32_t half = HID_KEY_DELAY_MS / 2 + 1;

    switch (st.type) {
        case StepType::Delay:
            s_nextAt = now + st.ms;
            s_idx++;
            break;

        case StepType::Combo:
            if (s_phase == 0 && st.mod) {
                // Modificador primeiro, sozinho: alguns hosts perdem o atalho
                // quando modificador e tecla chegam no mesmo relatório
                sendKeys(st.mod, 0);
                s_phase = 1;
                s_nextAt = now + 30;
            } else if (s_phase <= 1) {
                sendKeys(st.mod, st.key);
                s_phase = 2;
                s_nextAt = now + 40;  // combos seguram um pouco mais
            } else {
                releaseAll();
                s_phase = 0;
                s_idx++;
                s_nextAt = now + 60;
            }
            break;

        case StepType::Text: {
            if (s_phase == 0) {
                char c = st.text[s_pos];
                if (c == '\0') {
                    s_idx++;
                    s_pos = 0;
                    s_nextAt = now;
                    break;
                }
                if (!keymapLookup(settings::get().layout, c, s_cur)) {
                    s_pos++;  // caractere sem tecla: pula
                    break;
                }
                // Caps Lock ligado inverte o shift das letras
                bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
                if (letter && s_capsLock) s_cur.mod ^= HID_MOD_LSHIFT;
                sendKeys(s_cur.mod, s_cur.key);
                s_phase = 1;
                s_nextAt = now + half;
            } else if (s_phase == 1) {
                releaseAll();
                s_phase = s_cur.dead ? 2 : 0;
                if (!s_cur.dead) s_pos++;
                s_nextAt = now + half;
            } else if (s_phase == 2) {
                sendKeys(0, HID_KEY_SPACE);  // Espaço "solta" o acento da tecla morta
                s_phase = 3;
                s_nextAt = now + half;
            } else {
                releaseAll();
                s_phase = 0;
                s_pos++;
                s_nextAt = now + half;
            }
            break;
        }
    }
}

}  // namespace hid
