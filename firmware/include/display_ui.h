#pragma once
// Display ST7735 80x160 (usado em paisagem 160x80) + task de UI.
//
// A task de UI roda no core 0 e cuida do display, do LED e do botão. Assim a
// tela continua animando mesmo quando o loop principal está bloqueado dentro
// do callback de upload (que escreve na USB de forma bloqueante).
//
// O app altera o estado com UiLock:   { UiLock ui; ui->screen = UiScreen::Ready; }
#include "app_state.h"
#include "button.h"

namespace display {
void     begin();
// Próximo evento do botão (o clique que só acorda o backlight é descartado)
BtnEvent nextButtonEvent();
void     lock();
void     unlock();     // marca a tela para redesenho e conta como atividade
UiModel &model();      // acessar somente entre lock() e unlock()
}

struct UiLock {
    UiLock() { display::lock(); }
    ~UiLock() { display::unlock(); }
    UiLock(const UiLock &) = delete;
    UiLock &operator=(const UiLock &) = delete;
    UiModel *operator->() { return &display::model(); }
};
