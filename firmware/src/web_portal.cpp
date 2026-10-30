#include "web_portal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <dhcpserver/dhcpserver.h>
#include <esp_netif.h>
#include <esp_wifi.h>

#include "app.h"
#include "config.h"
#include "settings.h"
#include "usb_bridge.h"
#include "web_portal_html.h"

static WebServer       s_server(80);
static DNSServer       s_dns;
static const IPAddress AP_IP(AP_IP_BYTES);
static const char     *AP_HOST = "192.168.4.1";

// ---------- Estado do upload em andamento ----------
// Sem String no caminho de dados: só buffers estáticos e contadores.
struct UploadState {
    bool        active;     // aceito e repassando para a USB
    bool        finished;   // terminou com sucesso
    ErrorReason err;
    uint64_t    size;
    uint64_t    received;
    uint32_t    lastUi;
    uint32_t    speedAt;
    uint64_t    speedBytes;
    uint32_t    bps;
};
static UploadState s_up;
static char        s_name[128];

static const uint32_t UI_UPDATE_MS = 200;

// =============================================================================
// Utilitários
// =============================================================================

// Copia `src` para `dst` escapando para string JSON
static size_t jsonEscape(char *dst, size_t cap, const char *src) {
    size_t o = 0;
    for (; *src && o + 7 < cap; ++src) {
        unsigned char c = (unsigned char)*src;
        if (c == '"' || c == '\\') {
            dst[o++] = '\\';
            dst[o++] = c;
        } else if (c < 0x20) {
            o += snprintf(dst + o, cap - o, "\\u%04x", c);
        } else {
            dst[o++] = c;
        }
    }
    dst[o] = '\0';
    return o;
}

static void sendJson(int code, const char *json) {
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send(code, "application/json", json);
}

// Requisições para outros domínios (testes de conectividade do Android/iOS/
// Windows) são redirecionadas para o portal: é isso que faz o celular abrir
// a página sozinho ao conectar.
static bool captiveRedirect() {
    String host = s_server.hostHeader();
    if (host.length() == 0 || host == AP_HOST) return false;
    s_server.sendHeader("Location", String("http://") + AP_HOST + "/", true);
    s_server.send(302, "text/plain", "");
    return true;
}

// =============================================================================
// Upload (callbacks chamados pelo WebServer pedaço a pedaço)
// =============================================================================

static void uploadFail(ErrorReason why) {
    if (!s_up.active) return;
    s_up.active = false;
    s_up.err = why;
    usb_bridge::abortFile(why);
    app::transferFinished(why);
}

static void uploadStart(const char *fallbackName) {
    memset(&s_up, 0, sizeof(s_up));

    // Nome e tamanho vêm da query string (o WebServer já faz o URL-decode,
    // então acentos chegam como UTF-8). Uma vez por arquivo: String aqui é ok.
    const String &qn = s_server.arg("name");
    const char *name = qn.length() ? qn.c_str() : (fallbackName ? fallbackName : "arquivo");
    strncpy(s_name, name, sizeof(s_name) - 1);
    s_name[sizeof(s_name) - 1] = '\0';

    s_up.size = strtoull(s_server.arg("size").c_str(), nullptr, 10);
    uint16_t index = (uint16_t)s_server.arg("index").toInt();
    uint16_t count = (uint16_t)s_server.arg("count").toInt();

    ErrorReason e = usb_bridge::beginFile(s_name, s_up.size, index, count);
    if (e != ErrorReason::None) {
        // Recusa na hora: fecha a conexão para o celular não mandar o arquivo
        // inteiro à toa. A página consulta /status e mostra o motivo.
        s_up.err = e;
        s_server.client().stop();
        return;
    }
    s_up.active = true;
    s_up.speedAt = millis();
    app::transferStarted(s_name, s_up.size, index, count);
}

static void uploadData(const uint8_t *buf, size_t len) {
    if (!s_up.active || len == 0) return;
    if (s_up.received + len > s_up.size) {
        uploadFail(ErrorReason::Aborted);  // corpo maior que o anunciado
        s_server.client().stop();
        return;
    }
    // Bloqueia até o PC confirmar: o socket não é lido enquanto isso
    ErrorReason e = usb_bridge::writeChunk(buf, len);
    if (e != ErrorReason::None) {
        uploadFail(e);
        s_server.client().stop();
        return;
    }
    s_up.received += len;

    uint32_t now = millis();
    if (now - s_up.speedAt >= 1000) {
        uint32_t inst = (uint32_t)((s_up.received - s_up.speedBytes) * 1000 / (now - s_up.speedAt));
        s_up.bps = s_up.bps ? (s_up.bps * 6 + inst * 4) / 10 : inst;
        s_up.speedAt = now;
        s_up.speedBytes = s_up.received;
    }
    if (now - s_up.lastUi >= UI_UPDATE_MS) {
        s_up.lastUi = now;
        app::transferProgress(s_up.received, s_up.bps);
    }
}

static void uploadEnd() {
    if (!s_up.active) return;
    if (s_up.received != s_up.size) {
        uploadFail(ErrorReason::Aborted);
        return;
    }
    app::transferProgress(s_up.received, s_up.bps);
    ErrorReason e = usb_bridge::endFile();  // espera o PC validar o CRC
    if (e != ErrorReason::None) {
        uploadFail(e);
        return;
    }
    s_up.active = false;
    s_up.finished = true;
    app::transferFinished(ErrorReason::None);
}

static void uploadAborted() {
    // Cliente sumiu no meio (Wi-Fi caiu, página fechada ou cancelado)
    uploadFail(ErrorReason::WifiLost);
}

// Callback de corpo: cru (raw=1, usado pela página) ou multipart (curl -F)
static void handleUploadStream() {
    if (s_server.hasArg("raw")) {
        HTTPRaw &r = s_server.raw();
        switch (r.status) {
            case RAW_START:   uploadStart(nullptr); break;
            case RAW_WRITE:   uploadData(r.buf, r.currentSize); break;
            case RAW_END:     uploadEnd(); break;
            case RAW_ABORTED: uploadAborted(); break;
        }
    } else {
        HTTPUpload &u = s_server.upload();
        switch (u.status) {
            case UPLOAD_FILE_START:   uploadStart(u.filename.c_str()); break;
            case UPLOAD_FILE_WRITE:   uploadData(u.buf, u.currentSize); break;
            case UPLOAD_FILE_END:     uploadEnd(); break;
            case UPLOAD_FILE_ABORTED: uploadAborted(); break;
        }
    }
}

// Resposta depois que o corpo inteiro foi processado
static void handleUploadDone() {
    if (s_up.finished) {
        sendJson(200, "{\"ok\":true}");
        return;
    }
    ErrorReason e = s_up.err != ErrorReason::None ? s_up.err : ErrorReason::Aborted;
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"ok\":false,\"code\":%u,\"error\":\"%s\"}", (unsigned)e,
             e == ErrorReason::NoHost ? "Abra a pagina no PC" : errorText(e));
    sendJson(e == ErrorReason::NoHost ? 503 : 500, buf);
}

// =============================================================================
// Rotas
// =============================================================================

static void handleRoot() {
    if (captiveRedirect()) return;
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send_P(200, "text/html; charset=utf-8", PORTAL_HTML);
}

static void handleConfigPage() {
    s_server.send_P(200, "text/html; charset=utf-8", CONFIG_HTML);
}

static void handleStatus() {
    const Settings &s = settings::get();
    char url[160];
    jsonEscape(url, sizeof(url), s.pcUrl);
    char buf[288];
    snprintf(buf, sizeof(buf), "{\"host\":%s,\"busy\":%s,\"clients\":%u,\"fw\":\"%s\",\"url\":\"%s\"}",
             usb_bridge::hostAlive() ? "true" : "false", s_up.active ? "true" : "false",
             WiFi.softAPgetStationNum(), FW_VERSION, url);
    sendJson(200, buf);
}

static void handleConfigGet() {
    const Settings &s = settings::get();
    char url[160], cmd[160];
    jsonEscape(url, sizeof(url), s.pcUrl);
    jsonEscape(cmd, sizeof(cmd), s.linuxCmd);
    char buf[420];
    snprintf(buf, sizeof(buf), "{\"os\":%u,\"layout\":%u,\"url\":\"%s\",\"lxmode\":%u,\"lxcmd\":\"%s\"}",
             (unsigned)s.os, (unsigned)s.layout, url, s.linuxMode, cmd);
    sendJson(200, buf);
}

static void handleConfigPost() {
    Settings &s = settings::get();
    if (s_server.hasArg("os")) {
        int v = s_server.arg("os").toInt();
        if (v >= 0 && v < (int)TargetOS::Count) s.os = (TargetOS)v;
    }
    if (s_server.hasArg("layout")) {
        int v = s_server.arg("layout").toInt();
        if (v >= 0 && v < (int)KbLayout::Count) s.layout = (KbLayout)v;
    }
    if (s_server.hasArg("lxmode")) s.linuxMode = s_server.arg("lxmode").toInt() == 1 ? 1 : 0;
    if (s_server.arg("url").length()) {
        strncpy(s.pcUrl, s_server.arg("url").c_str(), sizeof(s.pcUrl) - 1);
        s.pcUrl[sizeof(s.pcUrl) - 1] = '\0';
    }
    if (s_server.arg("lxcmd").length()) {
        strncpy(s.linuxCmd, s_server.arg("lxcmd").c_str(), sizeof(s.linuxCmd) - 1);
        s.linuxCmd[sizeof(s.linuxCmd) - 1] = '\0';
    }
    settings::save();
    sendJson(200, "{\"ok\":true}");
}

static void handleNotFound() {
    if (captiveRedirect()) return;
    s_server.send(404, "text/plain", "404");
}

// Celular entrou/saiu do AP
static void onWifiEvent(arduino_event_id_t event, arduino_event_info_t) {
    if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED)
        app::stationsChanged(WiFi.softAPgetStationNum(), true);
    else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED)
        app::stationsChanged(WiFi.softAPgetStationNum(), false);
}

namespace web_portal {

void begin() {
    WiFi.mode(WIFI_AP);
    WiFi.onEvent(onWifiEvent);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, 0, AP_MAX_CLIENTS);
    // O 5º parâmetro (DNS) é obrigatório para o captive portal: no core 3.x o
    // DHCP só anuncia um servidor DNS aos clientes se ele for informado aqui.
    // Sem isso o celular não usa o nosso DNSServer e a página não abre sozinha.
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0), IPAddress(192, 168, 4, 2), AP_IP);

    // DHCP opção 114 (RFC 8910): Android 11+ e iOS 14+ abrem o portal direto
    // por ela, sem depender do teste de conectividade. O ponteiro precisa
    // continuar válido (o esp_netif não copia a string).
    static char portalUri[] = "http://192.168.4.1/";
    esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap) {
        esp_netif_dhcps_stop(ap);
        // reaplica o anúncio de DNS junto, para não depender do que o stop preserva
        dhcps_offer_t offerDns = OFFER_DNS;
        esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offerDns, sizeof(offerDns));
        esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, portalUri, strlen(portalUri));
        esp_netif_dhcps_start(ap);
    }
    WiFi.setSleep(false);              // sem economia de energia: mais vazão
    esp_wifi_set_ps(WIFI_PS_NONE);

    // DNS responde o IP do dongle para qualquer domínio (captive portal)
    s_dns.setErrorReplyCode(DNSReplyCode::NoError);
    s_dns.start(53, "*", AP_IP);

    s_server.on("/", HTTP_GET, handleRoot);
    s_server.on("/config", HTTP_GET, handleConfigPage);
    s_server.on("/status", HTTP_GET, handleStatus);
    s_server.on("/api/config", HTTP_GET, handleConfigGet);
    s_server.on("/api/config", HTTP_POST, handleConfigPost);
    s_server.on("/upload", HTTP_POST, handleUploadDone, handleUploadStream);
    s_server.onNotFound(handleNotFound);
    s_server.begin();
}

void loop() {
    s_dns.processNextRequest();
    s_server.handleClient();
}

}  // namespace web_portal
