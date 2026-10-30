#include "usb_bridge.h"

#include <Arduino.h>
#include <USB.h>
#include <USBCDC.h>
#include <stdarg.h>
#include <string.h>

#include "config.h"
#include "protocol.h"

// CDC própria (CDC_ON_BOOT=0): criada antes de USB.begin() junto com o HID
static USBCDC s_cdc(0);

static volatile bool s_mounted = false;
static volatile bool s_mountEvent = false;

using protocol::Type;
static constexpr uint8_t WINDOW = 4;
static constexpr size_t CHUNK = 4096;
static constexpr uint32_t HELLO_TIMEOUT_MS = 3500;
static constexpr uint32_t ACK_TIMEOUT_MS = 15000;
static constexpr uint32_t DONE_TIMEOUT_MS = 30000;
static bool s_seenHello = false;
static bool s_hostReady = false;
static bool s_helloAckPending = false;
static uint32_t s_lastHello = 0;
static uint32_t s_lastRx = 0;
static uint32_t s_nextId = 0;
// Um único frame em trânsito. Nenhum bloco de arquivo é guardado após o TX.
static uint8_t s_tx[protocol::HEADER + 12 + CHUNK + protocol::TRAILER];

enum class Phase : uint8_t { Idle, Starting, Sending, Finishing };
struct FileState {
    Phase phase = Phase::Idle;
    ErrorReason error = ErrorReason::None;
    uint32_t id = 0;
    uint64_t size = 0;
    uint64_t sent = 0;
    uint64_t acked = 0;
    uint32_t crc = 0;
    uint64_t pending[WINDOW] = {};
    uint8_t outstanding = 0;
    bool startAck = false;
    bool done = false;
};
static FileState s_file;

#if DEBUG_LOG
static portMUX_TYPE s_logMux = portMUX_INITIALIZER_UNLOCKED;
static char s_log[160];
static bool s_logPending = false;
// A macro pode ser usada por outras tasks: o loop é o único escritor da CDC.
void debugLog(const char *fmt, ...) {
    char message[sizeof(s_log)];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    portENTER_CRITICAL(&s_logMux);
    memcpy(s_log, message, sizeof(s_log));
    s_logPending = true;
    portEXIT_CRITICAL(&s_logMux);
}
#endif

static bool liveHost() {
    return s_mounted && static_cast<bool>(s_cdc) && s_seenHello && s_hostReady &&
           uint32_t(millis() - s_lastHello) < HELLO_TIMEOUT_MS;
}

static bool sendFrame(Type type, size_t len) {
    const size_t total = protocol::encode(type, s_tx + protocol::HEADER, len, s_tx, sizeof(s_tx));
    if (!total || !s_mounted || !s_cdc) return false;
    size_t written = 0;
    const uint32_t started = millis();
    while (written < total) {
        if (!s_mounted || !s_cdc || uint32_t(millis() - started) >= 1500) {
            s_seenHello = false;
            return false;
        }
        // O FIFO TX do core tem só 64 bytes. Não entrega um frame de 4 KiB
        // ao laço bloqueante interno: respeita o espaço anunciado e deixa a
        // task USB concluir cada pacote antes de oferecer o próximo.
        const int available = s_cdc.availableForWrite();
        if (available > 0) {
            size_t take = total - written;
            if (take > static_cast<size_t>(available)) take = available;
            if (take > 64) take = 64;
            const size_t sent = s_cdc.write(s_tx + written, take);
            if (sent > take) { s_seenHello = false; return false; }
            written += sent;
        }
        // Inclusive depois do último pacote: evita rajadas entre frames.
        delay(1);
    }
    return true;
}

static ErrorReason decodeReason(uint8_t value) {
    return value > 0 && value <= static_cast<uint8_t>(ErrorReason::Busy)
        ? static_cast<ErrorReason>(value) : ErrorReason::Aborted;
}

static void onFrame(Type type, const uint8_t *p, size_t len) {
    if (type == Type::Hello) {
        if (len != 2 || p[0] != protocol::VERSION) {
            s_seenHello = false;
            s_hostReady = false;
            return;
        }
        s_seenHello = true;
        s_hostReady = (p[1] & 1) != 0;
        s_lastHello = millis();
        s_helloAckPending = true;
        return;
    }
    if (s_file.phase == Phase::Idle || s_file.error != ErrorReason::None || len < 4 ||
        protocol::read32(p) != s_file.id) return;
    if (type == Type::Nak && len == 5) {
        s_file.error = decodeReason(p[4]);
    } else if (type == Type::Done && len == 5 && s_file.phase == Phase::Finishing) {
        s_file.done = true;
        if (p[4] != 0) s_file.error = decodeReason(p[4]);
    } else if (type == Type::Ack && len == 12) {
        const uint64_t offset = protocol::read64(p + 4);
        if (s_file.phase == Phase::Starting) {
            if (offset == 0) s_file.startAck = true;
            else s_file.error = ErrorReason::Crc;
            return;
        }
        if (s_file.phase != Phase::Sending || offset <= s_file.acked) return;
        // ACKs cumulativos só valem no fim de um DATA enviado. Duplicatas não
        // criam crédito e offsets no futuro ou no meio de um bloco abortam.
        for (uint8_t i = 0; i < s_file.outstanding; ++i) {
            if (s_file.pending[i] != offset) continue;
            const uint8_t consumed = i + 1;
            s_file.outstanding -= consumed;
            memmove(s_file.pending, s_file.pending + consumed, s_file.outstanding * sizeof(uint64_t));
            s_file.acked = offset;
            return;
        }
        s_file.error = ErrorReason::Crc;
    }
}

static void onCorrupt() {
    if (s_file.phase != Phase::Idle) s_file.error = ErrorReason::Crc;
}
static protocol::Parser s_parser(onFrame, onCorrupt);

// Esperas bombeiam HELLO/ACK/NAK e cedem CPU; não leem mais dados do Wi-Fi.
template <typename Predicate>
static ErrorReason waitFor(Predicate ready, uint32_t timeout = ACK_TIMEOUT_MS) {
    const uint32_t started = millis();
    for (;;) {
        usb_bridge::loop();
        if (s_file.error != ErrorReason::None) return s_file.error;
        if (!liveHost()) return ErrorReason::HostGone;
        if (ready()) return ErrorReason::None;
        if (uint32_t(millis() - started) >= timeout) return ErrorReason::HostGone;
        delay(1);
    }
}

static ErrorReason failFile(ErrorReason reason) {
    usb_bridge::abortFile(reason);
    return reason;
}

static void onUsbEvent(void *, esp_event_base_t base, int32_t id, void *) {
    if (base != ARDUINO_USB_EVENTS) return;
    switch (id) {
        case ARDUINO_USB_STARTED_EVENT:  // tud_mount_cb: host enumerou
            s_mounted = true;
            s_mountEvent = true;
            break;
        case ARDUINO_USB_STOPPED_EVENT:  // desmontado
            s_mounted = false;
            break;
        default:
            break;
    }
}

namespace usb_bridge {

void begin() {
    USB.VID(USB_VID_VALUE);
    USB.PID(USB_PID_VALUE);
    USB.manufacturerName(USB_MANUFACTURER);
    USB.productName(USB_PRODUCT);
    USB.onEvent(onUsbEvent);

    s_cdc.setRxBufferSize(4096);
    s_cdc.setTxTimeoutMs(0);  // sem espera ocupada: sendFrame controla o crédito
    // Mantém o "reset para modo de gravação" via DTR/RTS: o esptool usa isso no
    // `pio run -t upload`. A página Web Serial liga DTR, então não dispara.
    s_cdc.enableReboot(true);
    s_cdc.begin();

    USB.begin();
}

bool mounted() { return s_mounted; }

bool takeMountEvent() {
    if (!s_mountEvent) return false;
    s_mountEvent = false;
    return true;
}

void loop() {
    if (!s_mounted || !s_cdc) {
        s_seenHello = false;
        s_hostReady = false;
        s_helloAckPending = false;
        s_parser.reset();
        if (s_file.phase != Phase::Idle) s_file.error = ErrorReason::HostGone;
        return;
    }
    if (uint32_t(millis() - s_lastRx) > 500) s_parser.reset();
    uint8_t rx[128];
    // Limite por chamada para o restante do aplicativo continuar rodando.
    for (size_t budget = 0; budget < 4096 && s_cdc.available() > 0;) {
        const size_t read = s_cdc.read(rx, sizeof(rx));
        if (!read) break;
        s_lastRx = millis();
        s_parser.push(rx, read);
        budget += read;
    }
    if (s_helloAckPending) {
        s_helloAckPending = false;
        uint8_t *p = s_tx + protocol::HEADER;
        p[0] = protocol::VERSION; p[1] = WINDOW;
        protocol::put16(p + 2, CHUNK);
        memcpy(p + 4, FW_VERSION, strlen(FW_VERSION));
        sendFrame(Type::HelloAck, 4 + strlen(FW_VERSION));
    }
#if DEBUG_LOG
    // Logs nunca intercalam bytes com um frame de dados.
    size_t logLen = 0;
    portENTER_CRITICAL(&s_logMux);
    if (s_logPending) {
        logLen = strlen(s_log);
        memcpy(s_tx + protocol::HEADER, s_log, logLen);
        s_logPending = false;
    }
    portEXIT_CRITICAL(&s_logMux);
    if (logLen) sendFrame(Type::Log, logLen);
#endif
}

bool hostAlive() { return BRIDGE_NULL_SINK || liveHost(); }

ErrorReason beginFile(const char *name, uint64_t size, uint16_t index, uint16_t count) {
    if (BRIDGE_NULL_SINK) return ErrorReason::None;
    loop();
    if (s_file.phase != Phase::Idle) return ErrorReason::Busy;
    if (!liveHost()) return ErrorReason::NoHost;
    s_file = {};
    s_file.phase = Phase::Starting;
    if (++s_nextId == 0) ++s_nextId;
    s_file.id = s_nextId;
    s_file.size = size;
    uint8_t *p = s_tx + protocol::HEADER;
    protocol::put32(p, s_file.id);
    protocol::put64(p + 4, size);
    protocol::put16(p + 12, index);
    protocol::put16(p + 14, count);
    const size_t nameLen = strnlen(name, 127);
    memcpy(p + 16, name, nameLen);
    if (!sendFrame(Type::Start, 16 + nameLen)) return failFile(ErrorReason::HostGone);
    const ErrorReason result = waitFor([] { return s_file.startAck; });
    if (result != ErrorReason::None) return failFile(result);
    s_file.phase = Phase::Sending;
    return ErrorReason::None;
}

ErrorReason writeChunk(const uint8_t *data, size_t len) {
    if (BRIDGE_NULL_SINK) return ErrorReason::None;
    if (s_file.phase != Phase::Sending) return ErrorReason::Aborted;
    if (len > s_file.size - s_file.sent) return failFile(ErrorReason::Aborted);
    while (len) {
        const ErrorReason result = waitFor([] { return s_file.outstanding < WINDOW; });
        if (result != ErrorReason::None) return failFile(result);
        const size_t take = len < CHUNK ? len : CHUNK;
        uint8_t *p = s_tx + protocol::HEADER;
        protocol::put32(p, s_file.id);
        protocol::put64(p + 4, s_file.sent);
        memcpy(p + 12, data, take);
        if (!sendFrame(Type::Data, 12 + take)) return failFile(ErrorReason::HostGone);
        s_file.sent += take;
        s_file.pending[s_file.outstanding++] = s_file.sent;
        s_file.crc = protocol::crc32(s_file.crc, data, take);
        data += take; len -= take;
    }
    // Só devolve ao WebServer (que lerá mais do socket) com crédito livre.
    const ErrorReason result = waitFor([] { return s_file.outstanding < WINDOW; });
    return result == ErrorReason::None ? result : failFile(result);
}

ErrorReason endFile() {
    if (BRIDGE_NULL_SINK) return ErrorReason::None;
    if (s_file.phase != Phase::Sending) return ErrorReason::Aborted;
    if (s_file.sent != s_file.size) return failFile(ErrorReason::Aborted);
    const ErrorReason result = waitFor([] { return s_file.outstanding == 0; });
    if (result != ErrorReason::None) return failFile(result);
    s_file.phase = Phase::Finishing;
    uint8_t *p = s_tx + protocol::HEADER;
    protocol::put32(p, s_file.id);
    protocol::put32(p + 4, s_file.crc);
    if (!sendFrame(Type::End, 8)) return failFile(ErrorReason::HostGone);
    const ErrorReason done = waitFor([] { return s_file.done; }, DONE_TIMEOUT_MS);
    if (done != ErrorReason::None) return failFile(done);
    s_file.phase = Phase::Idle;
    return ErrorReason::None;
}

void abortFile(ErrorReason why) {
    if (s_file.phase == Phase::Idle) return;
    s_file.phase = Phase::Idle;
    s_file.error = why;
    uint8_t *p = s_tx + protocol::HEADER;
    protocol::put32(p, s_file.id);
    p[4] = static_cast<uint8_t>(why);
    sendFrame(Type::Abort, 5);
}

}  // namespace usb_bridge
