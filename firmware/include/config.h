#pragma once
// =============================================================================
// T-Dongle Bridge — configuração central
// Tudo que é "ajustável" fica aqui. Preferências trocadas em campo (SO alvo,
// layout, URL, brilho) são salvas em NVS e sobrepõem os padrões abaixo.
// =============================================================================

// ---------- Identidade ----------
#define FW_NAME    "T-Dongle Bridge"
#define FW_VERSION "0.1.0"

// ---------- Wi-Fi (modo AP) ----------
#define AP_SSID        "TDongle-Bridge"
#define AP_PASSWORD    "bridge1234"     // WPA2 exige no mínimo 8 caracteres
#define AP_CHANNEL     6
#define AP_MAX_CLIENTS 4
#define AP_IP_BYTES    192, 168, 4, 1

// ---------- Página Web Serial no PC (GitHub Pages) ----------
// Pode ser alterada em campo pelo portal do celular (fica salva em NVS).
#define DEFAULT_PC_URL "https://gianantonicci.github.io/arquivos-tdongle-s3/"

// ---------- USB ----------
// VID/PID registrados para o T-Dongle S3 (boards/dongles3.json da LilyGO).
// A página Web Serial filtra requestPort() por estes valores.
#define USB_VID_VALUE     0x303A
#define USB_PID_VALUE     0x82C1
#define USB_MANUFACTURER  "LilyGO"
#define USB_PRODUCT       "T-Dongle Bridge"

// ---------- HID (abrir o Chrome) ----------
// A URL é digitada sem "https://" e só com caracteres que ficam na mesma tecla
// em US e ABNT2 (letras, números, espaço, . - _ / , < > ! @ # $ % & * ( ) = +).
// Assim a sequência padrão funciona mesmo com o layout errado no menu; o layout
// só importa para comandos personalizados com outros símbolos (: ; ? ' " [ ] ...).
#define DEFAULT_TARGET_OS  3   // 0 = Windows, 1 = macOS, 2 = Linux, 3 = automático
#define DEFAULT_KB_LAYOUT  1   // 0 = US, 1 = ABNT2

// Linux: atalho do lançador + comando digitado. %URL% é substituído pela URL.
// Modos: 0 = Alt+F2 (GNOME/KDE/XFCE "executar comando")
//        1 = Ctrl+Alt+T (abre terminal; o comando roda em segundo plano)
#define DEFAULT_LINUX_MODE 0
#define DEFAULT_LINUX_CMD  "google-chrome %URL%"

#define HID_ENUM_DELAY_MS     2000  // espera após enumerar antes de digitar
#define HID_KEY_DELAY_MS      12    // intervalo entre teclas
#define HID_DIALOG_DELAY_MS   700   // espera a caixa Executar / Spotlight abrir
#define HID_SPOTLIGHT_SEARCH_MS 1200 // macOS: espera o Spotlight mostrar o resultado antes do Enter
#define HID_APP_OPEN_DELAY_MS 2500  // macOS: espera o Chrome abrir antes de Cmd+T

// ---------- Pinos (confirmados em github.com/Xinyuan-LilyGO/T-Dongle-S3) ----------
#define PIN_LCD_MOSI 3
#define PIN_LCD_SCLK 5
#define PIN_LCD_CS   4
#define PIN_LCD_DC   2
#define PIN_LCD_RST  1
#define PIN_LCD_BL   38   // ativo em LOW

#define PIN_LED_DATA 40   // APA102
#define PIN_LED_CLK  39

#define PIN_BUTTON   0    // BOOT

// ---------- Display ST7735 80x160 ----------
#define LCD_SPI_FREQ        27000000
#define LCD_OFFSET_X        26
#define LCD_OFFSET_Y        1
#define LCD_INVERT          true
#define LCD_RGB_ORDER       false   // se vermelho/azul vierem trocados, inverta
#define LCD_ROTATION        1       // 1 ou 3 (paisagem); 3 gira 180°
#define LCD_BRIGHTNESS      180     // 0..255
#define BACKLIGHT_TIMEOUT_MS 60000  // apaga após inatividade (0 = nunca)

#define UI_FRAME_MS          100    // período de redesenho de telas animadas
#define UI_INFO_ROTATE_MS    5000   // alternância automática Wi-Fi / QR
#define UI_MANUAL_PAGE_MS    30000  // após clique duplo, volta ao automático
#define UI_SUCCESS_MS        2000
#define UI_ERROR_MS          4000
#define UI_TOAST_MS          2000

// ---------- LED RGB ----------
#define LED_DEFAULT_LEVEL 2   // 1..5 (brilho global do APA102)

// ---------- Botão ----------
#define BTN_DEBOUNCE_MS 25
#define BTN_LONG_MS     800
#define BTN_DOUBLE_MS   300

// ---------- Debug ----------
// DEBUG_LOG vem do platformio.ini. Os logs viajam como frames LOG do protocolo,
// nunca como texto solto na CDC.
#ifndef DEBUG_LOG
#define DEBUG_LOG 0
#endif

#ifndef BRIDGE_NULL_SINK
#define BRIDGE_NULL_SINK 0
#endif

#ifndef UI_DEMO
#define UI_DEMO 0
#endif

#if DEBUG_LOG
void debugLog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define DBG(...) debugLog(__VA_ARGS__)
#else
#define DBG(...) do { } while (0)
#endif
