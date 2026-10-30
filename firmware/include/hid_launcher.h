#pragma once
// Sequência de teclado que abre o Chrome na página do PC.
// Não bloqueia: a sequência roda passo a passo em hid::tick().
#include <stdint.h>

enum class HidState : uint8_t { Idle, Waiting, Running };

namespace hid {
void     begin();                        // chamar antes de USB.begin()
// Agenda a sequência daqui a delayMs. Ignorado se já houver uma em andamento.
void     requestLaunch(uint32_t delayMs);
void     cancel();
HidState state();
void     tick(uint32_t now);
// Digita um texto qualquer (teste do layout, ver README)
void     requestType(const char *text);
}
