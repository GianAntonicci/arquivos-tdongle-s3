#include "display_ui.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "config.h"
#include "led.h"
#include "settings.h"

// =============================================================================
// Configuração do painel (ST7735S 80x160 IPS do T-Dongle S3)
// =============================================================================
class LGFX_TDongle : public lgfx::LGFX_Device {
    lgfx::Panel_ST7735S _panel;
    lgfx::Bus_SPI       _bus;
    lgfx::Light_PWM     _light;

  public:
    LGFX_TDongle() {
        {
            auto cfg = _bus.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = LCD_SPI_FREQ;
            cfg.freq_read = 16000000;
            cfg.spi_3wire = true;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = PIN_LCD_SCLK;
            cfg.pin_mosi = PIN_LCD_MOSI;
            cfg.pin_miso = -1;
            cfg.pin_dc = PIN_LCD_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs = PIN_LCD_CS;
            cfg.pin_rst = PIN_LCD_RST;
            cfg.pin_busy = -1;
            cfg.panel_width = 80;
            cfg.panel_height = 160;
            cfg.memory_width = 132;
            cfg.memory_height = 162;
            cfg.offset_x = LCD_OFFSET_X;
            cfg.offset_y = LCD_OFFSET_Y;
            cfg.offset_rotation = 0;
            cfg.readable = false;
            cfg.invert = LCD_INVERT;
            cfg.rgb_order = LCD_RGB_ORDER;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl = PIN_LCD_BL;
            cfg.invert = true;  // backlight ativo em LOW
            cfg.freq = 12000;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        setPanel(&_panel);
    }
};

static LGFX_TDongle s_lcd;
static LGFX_Sprite  s_canvas(&s_lcd);  // framebuffer 160x80x16 bits (25,6 KB)

static constexpr int W = 160;
static constexpr int H = 80;

// Paleta
static const uint16_t C_BG = lgfx::color565(0, 0, 0);
static const uint16_t C_FG = lgfx::color565(235, 235, 235);
static const uint16_t C_DIM = lgfx::color565(130, 130, 140);
static const uint16_t C_GREEN = lgfx::color565(40, 200, 90);
static const uint16_t C_DKGREEN = lgfx::color565(10, 70, 30);
static const uint16_t C_RED = lgfx::color565(230, 50, 50);
static const uint16_t C_CYAN = lgfx::color565(40, 200, 230);
static const uint16_t C_BLUE = lgfx::color565(60, 120, 255);
static const uint16_t C_PURPLE = lgfx::color565(170, 80, 255);
static const uint16_t C_PANEL = lgfx::color565(30, 30, 38);
static const uint16_t C_WHITE = lgfx::color565(255, 255, 255);

// =============================================================================
// Estado compartilhado
// =============================================================================
static SemaphoreHandle_t s_mutex;
static QueueHandle_t     s_btnQueue;
static UiModel           s_model;
static volatile bool     s_dirty = true;
static volatile uint32_t s_lastActivity = 0;
static bool              s_asleep = false;

// =============================================================================
// Utilitários de texto
// =============================================================================

// As fontes embutidas só têm ASCII: troca acentos Latin-1 por letras simples.
// Ex.: "Relatório Ação.pdf" -> "Relatorio Acao.pdf". Só para exibição.
static void foldAscii(const char *in, char *out, size_t cap) {
    static const char UP[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPs";
    static const char LO[] = "aaaaaaaceeeeiiiidnooooo/ouuuuypy";
    size_t o = 0;
    const uint8_t *p = (const uint8_t *)in;
    while (*p && o + 1 < cap) {
        uint8_t c = *p;
        if (c < 0x80) {
            out[o++] = (char)c;
            p++;
        } else if (c == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) {
            uint8_t d = p[1];
            out[o++] = d < 0xA0 ? UP[d - 0x80] : LO[d - 0xA0];
            p += 2;
        } else {
            // Outro caractere multibyte: pula o caractere inteiro
            out[o++] = '?';
            p++;
            while ((*p & 0xC0) == 0x80) p++;
        }
    }
    out[o] = '\0';
}

static void fmtBytes(uint64_t b, char *out, size_t cap) {
    if (b < 1024) snprintf(out, cap, "%u B", (unsigned)b);
    else if (b < 1024ULL * 1024) snprintf(out, cap, "%.1f KB", b / 1024.0);
    else if (b < 1024ULL * 1024 * 1024) snprintf(out, cap, "%.1f MB", b / 1048576.0);
    else snprintf(out, cap, "%.2f GB", b / 1073741824.0);
}

static void fmtSpeed(uint32_t bps, char *out, size_t cap) {
    if (bps < 1024 * 1024) snprintf(out, cap, "%u KB/s", (unsigned)(bps / 1024));
    else snprintf(out, cap, "%.2f MB/s", bps / 1048576.0);
}

// Escreve texto com rolagem horizontal (marquee) se não couber em `w` pixels
static void drawMarquee(const char *txt, int x, int y, int w, int h, uint32_t now) {
    auto &c = s_canvas;
    int tw = c.textWidth(txt);
    c.setClipRect(x, y, w, h);
    if (tw <= w) {
        c.drawString(txt, x, y);
    } else {
        const int gap = 30;
        int span = tw + gap;
        // pausa de 1 s no início de cada volta
        uint32_t t = now % (span * 30 + 1000);
        int off = t < 1000 ? 0 : (int)((t - 1000) / 30);
        c.drawString(txt, x - off, y);
        c.drawString(txt, x - off + span, y);
    }
    c.clearClipRect();
}

// =============================================================================
// Ícones desenhados com primitivas (sem bitmaps)
// =============================================================================

static void iconUsb(int cx, int cy, uint16_t col) {
    auto &c = s_canvas;
    // símbolo "tridente" do USB
    c.drawWideLine(cx, cy - 18, cx, cy + 14, 1.5f, col);
    c.fillTriangle(cx, cy - 24, cx - 5, cy - 16, cx + 5, cy - 16, col);
    c.fillCircle(cx, cy + 17, 5, col);
    c.drawWideLine(cx, cy + 6, cx - 11, cy - 2, 1.5f, col);
    c.drawWideLine(cx - 11, cy - 2, cx - 11, cy - 8, 1.5f, col);
    c.fillCircle(cx - 11, cy - 10, 3, col);
    c.drawWideLine(cx, cy + 1, cx + 11, cy - 6, 1.5f, col);
    c.drawWideLine(cx + 11, cy - 6, cx + 11, cy - 10, 1.5f, col);
    c.fillRect(cx + 8, cy - 16, 7, 7, col);
}

static void iconCheck(int cx, int cy, int r) {
    auto &c = s_canvas;
    c.fillCircle(cx, cy, r, C_GREEN);
    c.drawWideLine(cx - r / 2, cy, cx - r / 8, cy + r / 2 - 2, 2.5f, C_BG);
    c.drawWideLine(cx - r / 8, cy + r / 2 - 2, cx + r / 2 + 1, cy - r / 2 + 1, 2.5f, C_BG);
}

static void iconCross(int cx, int cy, int r) {
    auto &c = s_canvas;
    c.fillCircle(cx, cy, r, C_RED);
    int d = r / 2;
    c.drawWideLine(cx - d, cy - d, cx + d, cy + d, 2.5f, C_BG);
    c.drawWideLine(cx - d, cy + d, cx + d, cy - d, 2.5f, C_BG);
}

static void iconKeyboard(int x, int y, uint16_t col) {
    auto &c = s_canvas;
    c.drawRoundRect(x, y, 40, 24, 3, col);
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 6; ++k) c.fillRect(x + 4 + k * 6, y + 4 + r * 5, 4, 3, col);
    c.fillRect(x + 10, y + 19, 20, 2, col);
}

static void iconPhone(int x, int y, uint16_t col) {
    auto &c = s_canvas;
    c.drawRoundRect(x, y, 7, 11, 1, col);
    c.drawFastHLine(x + 2, y + 8, 3, col);
}

// =============================================================================
// Telas
// =============================================================================

// Barra superior com título e contador de celulares
static void header(const char *title, uint16_t bg, uint8_t stations) {
    auto &c = s_canvas;
    c.fillRect(0, 0, W, 12, bg);
    c.setFont(&fonts::Font0);
    c.setTextColor(C_WHITE);
    c.setTextDatum(textdatum_t::top_left);
    c.drawString(title, 3, 2);
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", stations);
    c.setTextDatum(textdatum_t::top_right);
    c.drawString(buf, W - 3, 2);
    iconPhone(W - 3 - c.textWidth(buf) - 10, 0, C_WHITE);
    c.setTextDatum(textdatum_t::top_left);
}

static void screenBoot() {
    auto &c = s_canvas;
    c.setTextColor(C_CYAN);
    c.setTextDatum(textdatum_t::middle_center);
    c.setFont(&fonts::Font4);
    c.drawString("T-Dongle", W / 2, 24);
    c.setFont(&fonts::Font2);
    c.setTextColor(C_FG);
    c.drawString("Bridge", W / 2, 48);
    c.setFont(&fonts::Font0);
    c.setTextColor(C_DIM);
    c.drawString("v" FW_VERSION, W / 2, 70);
}

static void screenWaitingHost(const UiModel &m, uint32_t now) {
    auto &c = s_canvas;
    bool on = (now / 500) % 2 == 0;  // pisca: o host ainda não respondeu
    iconUsb(24, 40, on ? C_BLUE : C_PANEL);
    c.setTextColor(C_FG);
    c.setTextDatum(textdatum_t::top_left);
    c.setFont(&fonts::Font2);
    c.drawString("Abra a", 50, 8);
    c.drawString("pagina no PC", 50, 26);
    c.setFont(&fonts::Font0);
    c.setTextColor(C_DIM);
    c.drawString("aguardando conexao", 50, 50);
    c.drawString("clique: abrir Chrome", 50, 64);
}

// Texto ao lado do QR code (coluna direita, x >= 84)
static void qrSide(const char *l1, const char *l2, const char *l3) {
    auto &c = s_canvas;
    c.setTextDatum(textdatum_t::top_left);
    c.setFont(&fonts::Font2);
    c.setTextColor(C_GREEN);
    c.drawString(l1, 84, 16);
    c.setFont(&fonts::Font0);
    c.setTextColor(C_FG);
    c.drawString(l2, 84, 40);
    c.setTextColor(C_DIM);
    c.drawString(l3, 84, 54);
}

// Escapa \ ; , : " no SSID/senha conforme o formato WIFI: dos QR codes
static void qrEscape(const char *in, char *out, size_t cap) {
    size_t o = 0;
    for (; *in && o + 2 < cap; ++in) {
        if (strchr("\\;,:\"", *in)) out[o++] = '\\';
        out[o++] = *in;
    }
    out[o] = '\0';
}

static void drawQr(const char *text) {
    auto &c = s_canvas;
    c.fillRect(0, 0, 80, 80, C_WHITE);
    c.qrcode(text, 2, 2, 76);
}

static void screenReady(const UiModel &m, uint32_t now) {
    auto &c = s_canvas;
    char buf[128];
    switch (m.infoPage) {
        case InfoPage::WifiText: {
            header("PRONTO", C_DKGREEN, m.stations);
            const char *labels[3] = {"Wi-Fi", "Senha", "IP"};
            const char *values[3] = {AP_SSID, AP_PASSWORD, "192.168.4.1"};
            for (int i = 0; i < 3; ++i) {
                int y = 16 + i * 21;
                c.setFont(&fonts::Font0);
                c.setTextColor(C_DIM);
                c.drawString(labels[i], 3, y + 4);
                c.setFont(&fonts::Font2);
                c.setTextColor(C_FG);
                c.drawString(values[i], 38, y);
            }
            break;
        }
        case InfoPage::QrWifi: {
            char ssid[48], pass[48];
            qrEscape(AP_SSID, ssid, sizeof(ssid));
            qrEscape(AP_PASSWORD, pass, sizeof(pass));
            snprintf(buf, sizeof(buf), "WIFI:S:%s;T:WPA;P:%s;;", ssid, pass);
            drawQr(buf);
            qrSide("Wi-Fi", "Escaneie p/", "conectar");
            break;
        }
        case InfoPage::QrUrl:
            drawQr("http://192.168.4.1/");
            qrSide("Enviar", "Abra a pagina", "192.168.4.1");
            break;
        case InfoPage::Status:
        default: {
            header("STATUS", C_PANEL, m.stations);
            const Settings &s = settings::get();
            uint32_t up = now / 1000;
            c.setFont(&fonts::Font0);
            c.setTextColor(C_FG);
            snprintf(buf, sizeof(buf), "PC: %s", m.hostAlive ? "conectado" : "ausente");
            c.drawString(buf, 3, 16);
            snprintf(buf, sizeof(buf), "Celulares: %u", m.stations);
            c.drawString(buf, 3, 26);
            snprintf(buf, sizeof(buf), "SO: %s  Tecl: %s", osName(s.os), layoutName(s.layout));
            c.drawString(buf, 3, 36);
            snprintf(buf, sizeof(buf), "RAM livre: %u KB",
                     (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024));
            c.drawString(buf, 3, 46);
            snprintf(buf, sizeof(buf), "Ligado: %02u:%02u:%02u", (unsigned)(up / 3600),
                     (unsigned)(up / 60 % 60), (unsigned)(up % 60));
            c.drawString(buf, 3, 56);
            c.setTextColor(C_DIM);
            c.drawString(FW_NAME " v" FW_VERSION, 3, 68);
            break;
        }
    }
}

static void screenTransfer(const UiModel &m, uint32_t now) {
    auto &c = s_canvas;
    const TransferView &x = m.xfer;
    char name[96], buf[32];
    foldAscii(x.name, name, sizeof(name));

    c.setTextDatum(textdatum_t::top_left);
    c.setFont(&fonts::Font2);
    c.setTextColor(C_FG);
    drawMarquee(name, 2, 0, W - 4, 16, now);

    c.setFont(&fonts::Font0);
    c.setTextColor(C_DIM);
    if (x.count > 1) {
        snprintf(buf, sizeof(buf), "arquivo %u de %u", x.index, x.count);
        c.drawString(buf, 2, 20);
    }
    fmtSpeed(x.bytesPerSec, buf, sizeof(buf));
    c.setTextDatum(textdatum_t::top_right);
    c.setTextColor(C_CYAN);
    c.drawString(buf, W - 2, 20);

    // Barra de progresso
    uint32_t pct = x.total ? (uint32_t)(x.done * 100 / x.total) : 0;
    if (pct > 100) pct = 100;
    c.drawRoundRect(2, 32, W - 4, 14, 3, C_DIM);
    int fill = (int)((W - 8) * pct / 100);
    if (fill > 0) c.fillRoundRect(4, 34, fill, 10, 2, C_CYAN);

    c.setTextDatum(textdatum_t::top_left);
    c.setFont(&fonts::Font4);
    c.setTextColor(C_FG);
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)pct);
    c.drawString(buf, 2, 52);

    char a[16], b[16];
    fmtBytes(x.done, a, sizeof(a));
    fmtBytes(x.total, b, sizeof(b));
    c.setFont(&fonts::Font0);
    c.setTextDatum(textdatum_t::top_right);
    c.setTextColor(C_DIM);
    c.drawString(a, W - 2, 56);
    snprintf(buf, sizeof(buf), "de %s", b);
    c.drawString(buf, W - 2, 68);
    c.setTextDatum(textdatum_t::top_left);
}

static void screenSuccess(const UiModel &m) {
    auto &c = s_canvas;
    char name[96], size[16];
    foldAscii(m.xfer.name, name, sizeof(name));
    fmtBytes(m.xfer.total, size, sizeof(size));
    iconCheck(22, 40, 18);
    c.setTextDatum(textdatum_t::top_left);
    c.setFont(&fonts::Font2);
    c.setTextColor(C_GREEN);
    c.drawString("Recebido!", 46, 12);
    c.setFont(&fonts::Font0);
    c.setTextColor(C_FG);
    c.setClipRect(46, 34, W - 48, 10);
    c.drawString(name, 46, 34);
    c.clearClipRect();
    c.setTextColor(C_DIM);
    c.drawString(size, 46, 50);
}

static void screenError(const UiModel &m) {
    auto &c = s_canvas;
    iconCross(22, 40, 18);
    c.setTextDatum(textdatum_t::top_left);
    c.setFont(&fonts::Font2);
    c.setTextColor(C_RED);
    c.drawString("ERRO", 46, 10);
    c.setTextColor(C_FG);
    c.drawString(errorText(m.error), 46, 30);
    if (m.xfer.name[0]) {
        char name[96];
        foldAscii(m.xfer.name, name, sizeof(name));
        c.setFont(&fonts::Font0);
        c.setTextColor(C_DIM);
        c.setClipRect(46, 54, W - 48, 10);
        c.drawString(name, 46, 54);
        c.clearClipRect();
    }
}

static void screenHid(uint32_t now) {
    auto &c = s_canvas;
    const Settings &s = settings::get();
    iconKeyboard(4, 28, C_PURPLE);
    c.setTextDatum(textdatum_t::top_left);
    c.setFont(&fonts::Font2);
    c.setTextColor(C_FG);
    c.drawString("Abrindo", 52, 12);
    char buf[24];
    int dots = (now / 300) % 4;
    snprintf(buf, sizeof(buf), "Chrome%.*s", dots, "...");
    c.drawString(buf, 52, 30);
    c.setFont(&fonts::Font0);
    c.setTextColor(C_DIM);
    snprintf(buf, sizeof(buf), "%s / %s", osName(s.os), layoutName(s.layout));
    c.drawString(buf, 52, 56);
}

static void screenMenu(const UiModel &m) {
    auto &c = s_canvas;
    const Settings &s = settings::get();
    c.fillRect(0, 0, W, 12, C_PURPLE);
    c.setFont(&fonts::Font0);
    c.setTextColor(C_WHITE);
    c.setTextDatum(textdatum_t::top_left);
    c.drawString("CONFIGURACAO", 3, 2);

    char items[(int)MenuItem::Count][24];
    snprintf(items[0], 24, "SO: %s", osName(s.os));
    snprintf(items[1], 24, "Teclado: %s", layoutName(s.layout));
    snprintf(items[2], 24, "Testar teclado");
    snprintf(items[3], 24, "LED: %u/5", s.ledLevel);
    snprintf(items[4], 24, "Info");
    snprintf(items[5], 24, "Sair");

    // Janela de 3 itens que acompanha a seleção
    const int rows = 3;
    int sel = m.menuIndex;
    int first = sel - 1;
    if (first < 0) first = 0;
    if (first > (int)MenuItem::Count - rows) first = (int)MenuItem::Count - rows;

    c.setFont(&fonts::Font2);
    for (int i = 0; i < rows; ++i) {
        int idx = first + i;
        int y = 14 + i * 18;
        if (idx == sel) {
            c.fillRoundRect(1, y, W - 2, 17, 3, C_PANEL);
            c.fillRect(1, y + 3, 3, 11, C_PURPLE);
        }
        c.setTextColor(idx == sel ? C_WHITE : C_DIM);
        c.drawString(items[idx], 8, y + 1);
    }
    c.setFont(&fonts::Font0);
    c.setTextColor(C_DIM);
    c.drawString("clique:prox  segurar:ok", 3, 71);
}

static void drawToast(const UiModel &m) {
    auto &c = s_canvas;
    c.fillRoundRect(6, 54, W - 12, 22, 4, C_BLUE);
    c.setFont(&fonts::Font2);
    c.setTextColor(C_WHITE);
    c.setTextDatum(textdatum_t::middle_center);
    c.drawString(m.toast, W / 2, 65);
    c.setTextDatum(textdatum_t::top_left);
}

static void render(const UiModel &m, uint32_t now) {
    auto &c = s_canvas;
    c.fillScreen(C_BG);
    c.setTextDatum(textdatum_t::top_left);
    switch (m.screen) {
        case UiScreen::Boot:        screenBoot(); break;
        case UiScreen::WaitingHost: screenWaitingHost(m, now); break;
        case UiScreen::Ready:       screenReady(m, now); break;
        case UiScreen::Transfer:    screenTransfer(m, now); break;
        case UiScreen::Success:     screenSuccess(m); break;
        case UiScreen::Error:       screenError(m); break;
        case UiScreen::HidRunning:  screenHid(now); break;
        case UiScreen::Menu:        screenMenu(m); break;
    }
    if ((int32_t)(m.toastUntil - now) > 0) drawToast(m);
    c.pushSprite(0, 0);
}

static bool isAnimated(const UiModel &m, uint32_t now) {
    switch (m.screen) {
        case UiScreen::WaitingHost:
        case UiScreen::Transfer:
        case UiScreen::HidRunning:
            return true;
        case UiScreen::Ready:
            return m.infoPage == InfoPage::Status;  // relógio
        default:
            return (int32_t)(m.toastUntil - now) > 0;
    }
}

// =============================================================================
// Task de UI: botão + LED + display
// =============================================================================
static void uiTask(void *) {
    UiModel snap;
    uint32_t lastFrame = 0;
    uint32_t pageSince = 0;
    UiScreen prevScreen = UiScreen::Boot;
    InfoPage prevPage = InfoPage::WifiText;
    bool toastWasOn = false;

    for (;;) {
        uint32_t now = millis();

        // Botão: com o backlight apagado o clique só acorda a tela
        BtnEvent ev = button::poll(now);
        if (ev != BtnEvent::None) {
            if (s_asleep) {
                s_lastActivity = now;
            } else {
                xQueueSend(s_btnQueue, &ev, 0);
                s_lastActivity = now;
            }
        }

        led::update(now);

        // Copia o modelo (e cuida da alternância automática das páginas)
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        if (s_model.screen != prevScreen || s_model.infoPage != prevPage) {
            pageSince = now;
            prevScreen = s_model.screen;
            prevPage = s_model.infoPage;
        }
        if (s_model.screen == UiScreen::Ready) {
            if (s_model.infoAuto && now - pageSince >= UI_INFO_ROTATE_MS) {
                // Automático alterna só Wi-Fi texto -> QR Wi-Fi -> QR URL
                uint8_t p = ((uint8_t)s_model.infoPage + 1) % 3;
                s_model.infoPage = (InfoPage)p;
                s_dirty = true;
            } else if (!s_model.infoAuto && now - pageSince >= UI_MANUAL_PAGE_MS) {
                s_model.infoAuto = true;
                s_model.infoPage = InfoPage::WifiText;
                s_dirty = true;
            }
        }
        bool toastOn = (int32_t)(s_model.toastUntil - now) > 0;
        if (toastOn != toastWasOn) {
            s_dirty = true;  // aviso apareceu ou sumiu
            toastWasOn = toastOn;
        }
        bool dirty = s_dirty;
        s_dirty = false;
        snap = s_model;
        xSemaphoreGive(s_mutex);

        // Backlight: apaga após inatividade só nas telas de espera
        bool idleScreen = snap.screen == UiScreen::Ready || snap.screen == UiScreen::WaitingHost;
        bool shouldSleep = BACKLIGHT_TIMEOUT_MS > 0 && idleScreen &&
                           now - s_lastActivity >= BACKLIGHT_TIMEOUT_MS;
        if (shouldSleep && !s_asleep) {
            s_asleep = true;
            s_lcd.setBrightness(0);
        } else if (!shouldSleep && s_asleep) {
            s_asleep = false;
            s_lcd.setBrightness(LCD_BRIGHTNESS);
            dirty = true;
        }

        if (!s_asleep && (dirty || (isAnimated(snap, now) && now - lastFrame >= UI_FRAME_MS))) {
            render(snap, now);
            lastFrame = now;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

namespace display {

void begin() {
    s_mutex = xSemaphoreCreateMutex();
    s_btnQueue = xQueueCreate(8, sizeof(BtnEvent));
    memset(&s_model, 0, sizeof(s_model));
    s_model.screen = UiScreen::Boot;
    s_model.infoAuto = true;

    s_lcd.init();
    s_lcd.setRotation(LCD_ROTATION);
    s_lcd.setBrightness(LCD_BRIGHTNESS);
    s_lcd.fillScreen(TFT_BLACK);
    s_canvas.setColorDepth(16);
    s_canvas.createSprite(W, H);

    s_lastActivity = millis();
    // Core 0, prioridade baixa: o Wi-Fi (também no core 0) tem prioridade maior
    xTaskCreatePinnedToCore(uiTask, "ui", 6144, nullptr, 1, nullptr, 0);
}

BtnEvent nextButtonEvent() {
    BtnEvent ev;
    if (xQueueReceive(s_btnQueue, &ev, 0) == pdTRUE) return ev;
    return BtnEvent::None;
}

void lock() { xSemaphoreTake(s_mutex, portMAX_DELAY); }

void unlock(bool changed) {
    if (changed) {
        s_dirty = true;
        s_lastActivity = millis();
    }
    xSemaphoreGive(s_mutex);
}

UiModel &model() { return s_model; }

}  // namespace display
