#pragma once
// Preferências persistentes em NVS. Só configurações — nunca dados de arquivos.
#include "app_state.h"

struct Settings {
    TargetOS os;
    KbLayout layout;
    uint8_t  ledLevel;        // 1..5
    uint8_t  linuxMode;       // ver DEFAULT_LINUX_MODE em config.h
    char     pcUrl[128];
    char     linuxCmd[128];   // %URL% é substituído pela URL
};

namespace settings {
void      load();
void      save();
Settings &get();
}
