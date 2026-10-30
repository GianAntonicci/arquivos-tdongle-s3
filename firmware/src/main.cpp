// T-Dongle Bridge — ponte Wi-Fi -> USB (nada é armazenado no dongle)
#include <Arduino.h>

#include "app.h"
#include "app_state.h"
#include "button.h"
#include "config.h"
#include "display_ui.h"
#include "hid_launcher.h"
#include "led.h"
#include "settings.h"
#include "usb_bridge.h"
#include "web_portal.h"

// Texto digitado por "Testar teclado" (ver README: validação do layout)
static const char KB_TEST_TEXT[] =
    "abc XYZ 0123456789 !@#$%&*()-_=+ []{}\\| ;:'\",.<>/? ~^` https://a.b/c?d=e_f";

// ---------- Estado do app ----------
static bool     s_hostAlive = false;
static uint32_t s_screenUntil = 0;   // telas temporárias (sucesso/erro/HID)
static uint32_t s_menuLastInput = 0;
static bool     s_autoLaunched = false;  // HID automático: uma vez por plug
static bool     s_hidShown = false;      // tela "Abrindo Chrome" ativa
static const uint32_t MENU_TIMEOUT_MS = 20000;

// Troca de tela e ajusta o LED correspondente
static void setScreen(UiScreen sc, uint32_t holdMs = 0) {
    {
        UiLock ui;
        ui->screen = sc;
        if (sc == UiScreen::Ready) {
            ui->infoAuto = true;
            ui->infoPage = InfoPage::WifiText;
        }
    }
    s_screenUntil = holdMs ? millis() + holdMs : 0;
    switch (sc) {
        case UiScreen::WaitingHost: led::setPattern(LedPattern::WaitingHost); break;
        case UiScreen::Ready:       led::setPattern(LedPattern::Ready); break;
        case UiScreen::Transfer:    led::setPattern(LedPattern::Transfer); break;
        case UiScreen::Success:
            led::setPattern(LedPattern::Ready);
            led::flashSuccess();
            break;
        case UiScreen::Error:       led::setPattern(LedPattern::Error); break;
        case UiScreen::HidRunning:  led::setPattern(LedPattern::Hid); break;
        default: break;
    }
}

// Tela "de repouso": Pronto se o PC está conectado, senão Aguardando
static void goIdle() { setScreen(s_hostAlive ? UiScreen::Ready : UiScreen::WaitingHost); }

static UiScreen currentScreen() {
    UiReadLock ui;
    return ui->screen;
}

static bool isIdleScreen(UiScreen sc) { return sc == UiScreen::Ready || sc == UiScreen::WaitingHost; }

// ---------- Menu de configuração ----------
static void openMenu() {
    {
        UiLock ui;
        ui->screen = UiScreen::Menu;
        ui->menuIndex = 0;
    }
    s_screenUntil = 0;
    s_menuLastInput = millis();
    button::setDoubleClickEnabled(false);
}

static void closeMenu() {
    settings::save();
    button::setDoubleClickEnabled(true);
    goIdle();
}

static void menuActivate() {
    Settings &s = settings::get();
    uint8_t idx;
    {
        UiReadLock ui;
        idx = ui->menuIndex;
    }
    switch ((MenuItem)idx) {
        case MenuItem::Os:
            s.os = (TargetOS)(((uint8_t)s.os + 1) % (uint8_t)TargetOS::Count);
            break;
        case MenuItem::Layout:
            s.layout = (KbLayout)(((uint8_t)s.layout + 1) % (uint8_t)KbLayout::Count);
            break;
        case MenuItem::KbTest:
            // Abra um editor de texto no PC antes: o texto é digitado onde estiver o foco
            closeMenu();
            hid::requestType(KB_TEST_TEXT);
            return;
        case MenuItem::Led:
            s.ledLevel = s.ledLevel % 5 + 1;
            led::setLevel(s.ledLevel);
            break;
        case MenuItem::Info:
            settings::save();
            button::setDoubleClickEnabled(true);
            setScreen(UiScreen::Ready);
            {
                UiLock ui;
                ui->infoAuto = false;
                ui->infoPage = InfoPage::Status;
            }
            return;
        case MenuItem::Exit:
        default:
            closeMenu();
            return;
    }
    settings::save();  // salva na hora: desplugar dentro do menu não perde a troca
    UiLock ui;         // redesenha com o novo valor
}

// ---------- Ações do botão ----------
[[maybe_unused]] static void launchChrome() {  // no modo demo o clique curto é da demo
    if (!usb_bridge::mounted()) return;  // ligado num carregador: não há PC
    hid::requestLaunch(0);
}

// Acompanha a sequência HID: mostra "Abrindo Chrome" enquanto digita
static void updateHid(uint32_t now) {
    if (usb_bridge::takeMountEvent() && !s_autoLaunched) {
        s_autoLaunched = true;
        // Se a página já reconectou sozinha, o Chrome já está aberto
        if (!usb_bridge::hostAlive()) hid::requestLaunch(HID_ENUM_DELAY_MS);
    }
    hid::tick(now);

    bool running = hid::state() == HidState::Running;
    if (running && !s_hidShown) {
        s_hidShown = true;
        setScreen(UiScreen::HidRunning);
    } else if (!running && s_hidShown && hid::state() == HidState::Idle) {
        s_hidShown = false;
        goIdle();
    }
}

static void nextInfoPage() {
    if (currentScreen() != UiScreen::Ready) return;
    UiLock ui;
    ui->infoAuto = false;
    ui->infoPage = (InfoPage)(((uint8_t)ui->infoPage + 1) % (uint8_t)InfoPage::Count);
}

#if UI_DEMO
// ---------- Modo demonstração: clique curto percorre as telas ----------
static uint8_t  s_demoStep = 0;
static uint32_t s_demoT0 = 0;

static void demoStep() {
    uint32_t now = millis();
    s_demoT0 = now;
    switch (s_demoStep++ % 8) {
        case 0:
            s_hostAlive = false;
            setScreen(UiScreen::WaitingHost);
            break;
        case 1:
            s_hostAlive = true;
            setScreen(UiScreen::Ready);
            {
                UiLock ui;
                ui->hostAlive = true;
                ui->stations = 1;
                strcpy(ui->toast, "Celular conectado");
                ui->toastUntil = now + UI_TOAST_MS;
            }
            break;
        case 2: {
            setScreen(UiScreen::Transfer);
            UiLock ui;
            strcpy(ui->xfer.name, "Férias em família - vídeo 4K da praia.mp4");
            ui->xfer.total = 734003200ULL;
            ui->xfer.done = 0;
            ui->xfer.bytesPerSec = 0;
            ui->xfer.index = 2;
            ui->xfer.count = 5;
            break;
        }
        case 3:
            setScreen(UiScreen::Success);
            break;
        case 4: {
            setScreen(UiScreen::Error);
            UiLock ui;
            ui->error = ErrorReason::WifiLost;
            break;
        }
        case 5:
            setScreen(UiScreen::HidRunning);
            break;
        case 6:
            openMenu();
            break;
        case 7:
            closeMenu();
            nextInfoPage();
            {
                UiLock ui;
                ui->infoPage = InfoPage::Status;
            }
            break;
    }
}

static void demoTick() {
    // Simula progresso da transferência a ~1,1 MB/s
    static uint32_t last = 0;
    if (millis() - last < UI_FRAME_MS || currentScreen() != UiScreen::Transfer) return;
    last = millis();
    uint32_t el = millis() - s_demoT0;
    UiLock ui;
    ui->xfer.bytesPerSec = 1150000;
    uint64_t d = (uint64_t)el * 1150;  // bytes
    ui->xfer.done = d > ui->xfer.total ? ui->xfer.total : d;
}
#endif

static void handleButton(BtnEvent ev) {
    UiScreen sc = currentScreen();

    if (sc == UiScreen::Menu) {
        s_menuLastInput = millis();
        if (ev == BtnEvent::Short) {
            UiLock ui;
            ui->menuIndex = (ui->menuIndex + 1) % (uint8_t)MenuItem::Count;
        } else if (ev == BtnEvent::Long) {
            menuActivate();
        }
        return;
    }

    switch (ev) {
        case BtnEvent::Short:
#if UI_DEMO
            demoStep();
#else
            if (sc != UiScreen::Transfer) launchChrome();
#endif
            break;
        case BtnEvent::Double:
            nextInfoPage();
            break;
        case BtnEvent::Long:
            if (sc != UiScreen::Transfer) openMenu();
            break;
        default:
            break;
    }
}

// =============================================================================
// Eventos vindos do portal e da ponte USB (app.h)
// =============================================================================
namespace app {

void transferStarted(const char *name, uint64_t size, uint16_t index, uint16_t count) {
    if (currentScreen() == UiScreen::Menu) {
        settings::save();
        button::setDoubleClickEnabled(true);
    }
    {
        UiLock ui;
        strncpy(ui->xfer.name, name, sizeof(ui->xfer.name) - 1);
        ui->xfer.name[sizeof(ui->xfer.name) - 1] = '\0';
        ui->xfer.total = size;
        ui->xfer.done = 0;
        ui->xfer.bytesPerSec = 0;
        ui->xfer.index = index;
        ui->xfer.count = count;
        ui->error = ErrorReason::None;
    }
    setScreen(UiScreen::Transfer);
}

void transferProgress(uint64_t done, uint32_t bytesPerSec) {
    UiLock ui;
    ui->xfer.done = done;
    ui->xfer.bytesPerSec = bytesPerSec;
}

void transferFinished(ErrorReason result) {
    if (result == ErrorReason::None) {
        setScreen(UiScreen::Success, UI_SUCCESS_MS);
        return;
    }
    {
        UiLock ui;
        ui->error = result;
    }
    setScreen(UiScreen::Error, UI_ERROR_MS);
}

// Chamado pela task de eventos do Wi-Fi
void stationsChanged(uint8_t count, bool joined) {
    UiLock ui;
    ui->stations = count;
    if (ui->screen == UiScreen::Ready || ui->screen == UiScreen::WaitingHost) {
        strncpy(ui->toast, joined ? "Celular conectado" : "Celular saiu", sizeof(ui->toast) - 1);
        ui->toastUntil = millis() + UI_TOAST_MS;
    }
}

}  // namespace app

// Página do PC apareceu/sumiu (HELLO): troca Aguardando <-> Pronto
static void updateHost() {
    bool alive = usb_bridge::hostAlive();
    if (alive == s_hostAlive) return;
    s_hostAlive = alive;
    {
        UiLock ui;
        ui->hostAlive = alive;
    }
    // A página já está aberta: não precisa digitar nada
    if (alive && hid::state() == HidState::Waiting) hid::cancel();
    if (isIdleScreen(currentScreen())) goIdle();
}

// =============================================================================

void setup() {
    settings::load();
    led::begin();
    led::setLevel(settings::get().ledLevel);
    button::begin();
    display::begin();  // tela de boot + task de UI

    hid::begin();      // registra o teclado antes de subir a USB
    usb_bridge::begin();
    web_portal::begin();

    setScreen(UiScreen::Boot, 1500);
}

void loop() {
    uint32_t now = millis();

    for (BtnEvent ev; (ev = display::nextButtonEvent()) != BtnEvent::None;) handleButton(ev);

    // Telas temporárias voltam ao repouso
    if (s_screenUntil && (int32_t)(now - s_screenUntil) >= 0) {
        s_screenUntil = 0;
        goIdle();
    }

    if (currentScreen() == UiScreen::Menu && now - s_menuLastInput > MENU_TIMEOUT_MS) closeMenu();

    updateHid(now);
    updateHost();

    usb_bridge::loop();
    web_portal::loop();  // durante um upload, fica aqui dentro até terminar

#if UI_DEMO
    demoTick();
#endif

    delay(2);
}
