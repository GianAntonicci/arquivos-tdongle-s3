#include "settings.h"

#include <Preferences.h>
#include <string.h>

#include "config.h"

static Settings s_cfg;
static const char *NVS_NS = "bridge";

// Copia com terminação garantida
static void copyStr(char *dst, size_t cap, const char *src) {
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

namespace settings {

void load() {
    Preferences p;
    p.begin(NVS_NS, true);
    // Chave nova: instalações anteriores também passam a detectar ao conectar.
    uint8_t os = p.getUChar("osmode", DEFAULT_TARGET_OS);
    uint8_t layout = p.getUChar("layout", DEFAULT_KB_LAYOUT);
    s_cfg.os = os < (uint8_t)TargetOS::Count ? (TargetOS)os : TargetOS::Auto;
    s_cfg.layout = layout < (uint8_t)KbLayout::Count ? (KbLayout)layout : KbLayout::US;
    s_cfg.ledLevel = p.getUChar("led", LED_DEFAULT_LEVEL);
    if (s_cfg.ledLevel < 1 || s_cfg.ledLevel > 5) s_cfg.ledLevel = LED_DEFAULT_LEVEL;
    s_cfg.linuxMode = p.getUChar("lxmode", DEFAULT_LINUX_MODE);

    // getString(key, buf, len) devolve 0 se a chave não existir
    if (p.getString("url", s_cfg.pcUrl, sizeof(s_cfg.pcUrl)) == 0)
        copyStr(s_cfg.pcUrl, sizeof(s_cfg.pcUrl), DEFAULT_PC_URL);
    if (p.getString("lxcmd", s_cfg.linuxCmd, sizeof(s_cfg.linuxCmd)) == 0)
        copyStr(s_cfg.linuxCmd, sizeof(s_cfg.linuxCmd), DEFAULT_LINUX_CMD);
    p.end();
}

void save() {
    Preferences p;
    p.begin(NVS_NS, false);
    p.putUChar("osmode", (uint8_t)s_cfg.os);
    p.putUChar("layout", (uint8_t)s_cfg.layout);
    p.putUChar("led", s_cfg.ledLevel);
    p.putUChar("lxmode", s_cfg.linuxMode);
    p.putString("url", s_cfg.pcUrl);
    p.putString("lxcmd", s_cfg.linuxCmd);
    p.end();
}

Settings &get() { return s_cfg; }

}  // namespace settings

// ---------- Nomes legíveis (usados na UI e no portal) ----------

const char *osName(TargetOS os) {
    switch (os) {
        case TargetOS::Windows: return "Windows";
        case TargetOS::MacOS:   return "macOS";
        case TargetOS::Linux:   return "Linux";
        case TargetOS::Auto:    return "Auto";
        default:                return "?";
    }
}

const char *layoutName(KbLayout l) {
    switch (l) {
        case KbLayout::US:    return "US";
        case KbLayout::ABNT2: return "ABNT2";
        default:              return "?";
    }
}

const char *errorText(ErrorReason e) {
    switch (e) {
        case ErrorReason::Crc:       return "CRC invalido";
        case ErrorReason::HostGone:  return "PC desconectou";
        case ErrorReason::WifiLost:  return "Wi-Fi caiu";
        case ErrorReason::Aborted:   return "Abortado";
        case ErrorReason::NoHost:    return "PC nao pronto";
        case ErrorReason::DiskError: return "Erro no disco";
        case ErrorReason::Busy:      return "Ocupado";
        default:                     return "Erro";
    }
}
