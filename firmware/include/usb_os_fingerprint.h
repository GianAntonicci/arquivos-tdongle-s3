#pragma once
#include "app_state.h"

// Heurística dos tamanhos pedidos para descritores STRING (não de configuração).
// Referência dos padrões: https://docs.qmk.fm/features/os_detection
// Sem assinatura conhecida, permanece Auto: não arrisca um atalho de outro SO.
struct UsbOsFingerprint {
    uint16_t count = 0;
    uint16_t short2 = 0;
    uint16_t short4 = 0;
    uint16_t full = 0;

    void observe(uint16_t length) {
        // Limita a coleta para não transbordar nem reagir a consultas tardias.
        if (count >= 32) return;
        ++count;
        if (length == 2) ++short2;
        if (length == 4) ++short4;
        if (length == 255) ++full;
    }

    TargetOS detected() const {
        if (full >= 2 && short4 >= 1) return TargetOS::Windows;
        if (count >= 5 && short2 >= 2 && full >= 1) return TargetOS::MacOS;
        if (count >= 3 && full == count) return TargetOS::Linux;
        return TargetOS::Auto;
    }
};
