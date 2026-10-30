#pragma once
#include "app_state.h"

namespace host_os {
// Auto significa que o padrão USB ainda não foi reconhecido.
TargetOS detected();
// Respeita a escolha manual; em Auto usa somente a conexão atual, sem NVS.
TargetOS effective();
}
