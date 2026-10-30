#pragma once
// Botão BOOT (GPIO0): clique curto, clique duplo e pressão longa.
#include <stdint.h>

enum class BtnEvent : uint8_t { None, Short, Double, Long };

namespace button {
void     begin();
// Chamar periodicamente (~10–20 ms). Devolve no máximo um evento por chamada.
BtnEvent poll(uint32_t now);
// Sem clique duplo o clique curto sai na hora (navegação do menu fica ágil)
void     setDoubleClickEnabled(bool en);
}
