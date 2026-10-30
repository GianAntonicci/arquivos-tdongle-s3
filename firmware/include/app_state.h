#pragma once
// Tipos compartilhados entre os módulos (UI, LED, HID, ponte USB).
#include <stdint.h>

enum class TargetOS : uint8_t { Windows = 0, MacOS, Linux, Auto, Count };
enum class KbLayout : uint8_t { US = 0, ABNT2, Count };

// Telas principais do display
enum class UiScreen : uint8_t {
    Boot,
    WaitingHost,  // "Abra a página no PC"
    Ready,        // info Wi-Fi / QR / status
    Transfer,
    Success,
    Error,
    HidRunning,   // "Abrindo Chrome"
    Menu,
};

// Sub-páginas da tela Pronto (clique duplo alterna)
enum class InfoPage : uint8_t { WifiText = 0, QrWifi, QrUrl, Status, Count };

enum class ErrorReason : uint8_t {
    None = 0,
    Crc,          // CRC do arquivo ou de um frame não bateu
    HostGone,     // página do PC parou de responder
    WifiLost,     // celular caiu no meio do upload
    Aborted,      // cancelado pelo celular ou pelo PC
    NoHost,       // upload recusado: PC não conectado
    DiskError,    // PC não conseguiu gravar
    Busy,         // outra transferência em andamento
};

// Itens do menu de configuração
enum class MenuItem : uint8_t { Os = 0, Layout, KbTest, Led, Info, Exit, Count };

struct TransferView {
    char     name[96];     // nome do arquivo (UTF-8)
    uint64_t total;        // bytes
    uint64_t done;         // bytes
    uint32_t bytesPerSec;
    uint16_t index;        // 1..count (0 = desconhecido)
    uint16_t count;
};

// Estado mostrado no display. Escrito pelo app sob UiLock, lido pela task de UI.
struct UiModel {
    UiScreen     screen;
    InfoPage     infoPage;
    bool         infoAuto;     // alternância automática das páginas
    bool         hostAlive;    // HELLO recebido nos últimos segundos
    uint8_t      stations;     // celulares conectados ao AP
    uint32_t     toastUntil;   // millis() até quando mostrar o aviso
    char         toast[32];
    TransferView xfer;
    ErrorReason  error;
    uint8_t      menuIndex;
};

const char *osName(TargetOS os);
const char *layoutName(KbLayout l);
const char *errorText(ErrorReason e);
