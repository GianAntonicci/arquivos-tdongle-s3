// Teste da ponte real com uma CDC simulada e um receptor que valida os frames.
#include "../src/usb_bridge.cpp"
#include <assert.h>
#include <vector>

static bool autoAck = true, autoStartAck = true, autoDone = true, rejectStart = false, wrongDone = false;
static uint32_t peerId, peerCrc;
static uint64_t peerSize, peerReceived;
static unsigned starts, ends, aborts, helloAcks, dataFrames;
static std::vector<uint64_t> waiting;
static std::vector<uint8_t> transcript;

static void reply(Type type, const uint8_t *p, size_t len) {
    uint8_t frame[32];
    const size_t n = protocol::encode(type, p, len, frame, sizeof(frame));
    s_cdc.rx.insert(s_cdc.rx.end(), frame, frame + n);
}
static void hello(bool ready = true, uint8_t version = 1) {
    const uint8_t payload[] = {version, static_cast<uint8_t>(ready)};
    reply(Type::Hello, payload, 2);
}
static void ack(uint64_t offset, uint32_t id = 0) {
    uint8_t p[12]; protocol::put32(p, id ? id : peerId); protocol::put64(p + 4, offset);
    reply(Type::Ack, p, sizeof(p));
}
static void receive(Type type, const uint8_t *p, size_t len) {
    if (type == Type::HelloAck) {
        assert(len >= 4 && p[0] == 1 && p[1] == 4 && protocol::read16(p + 2) == 4096);
        ++helloAcks;
    } else if (type == Type::Start) {
        ++starts; peerId = protocol::read32(p); peerSize = protocol::read64(p + 4);
        peerReceived = 0; peerCrc = 0;
        if (rejectStart) {
            uint8_t nak[5]; protocol::put32(nak, peerId); nak[4] = 6;
            reply(Type::Nak, nak, 5);
        } else if (autoStartAck) ack(0);
    } else if (type == Type::Data) {
        assert(len <= 4108 && protocol::read32(p) == peerId);
        assert(protocol::read64(p + 4) == peerReceived);
        ++dataFrames;
        peerReceived += len - 12;
        peerCrc = protocol::crc32(peerCrc, p + 12, len - 12);
        if (autoAck) ack(peerReceived);
        else waiting.push_back(peerReceived);
    } else if (type == Type::End) {
        ++ends;
        assert(peerReceived == peerSize && protocol::read32(p + 4) == peerCrc);
        if (autoDone) {
            uint8_t done[5]; protocol::put32(done, peerId); done[4] = wrongDone ? 1 : 0;
            reply(Type::Done, done, 5);
        }
    } else if (type == Type::Abort) ++aborts;
}
static protocol::Parser peer(receive, [] { assert(false); });
static void reset() {
    s_file = {}; s_seenHello = false; s_hostReady = false; s_helloAckPending = false;
    s_parser.reset(); peer.reset(); s_cdc.rx.clear(); s_cdc.connected = true; s_cdc.burst = 64;
    s_cdc.capacity = 64; s_cdc.readyAt = 0; s_cdc.largestWrite = 0;
    s_cdc.peer = [](const uint8_t *p, size_t len) {
        transcript.insert(transcript.end(), p, p + len);
        peer.push(p, len);
    };
    fakeDelay = nullptr; fakeNow = 0; s_lastRx = 0;
    autoAck = autoStartAck = autoDone = true; rejectStart = wrongDone = false; waiting.clear();
    starts = ends = aborts = helloAcks = dataFrames = 0;
    transcript.clear();
    onUsbEvent(nullptr, ARDUINO_USB_EVENTS, ARDUINO_USB_STARTED_EVENT, nullptr);
}
static void keepAlive() { if (fakeNow % 500 == 0) hello(); }
int main(int argc, char **argv) {
    usb_bridge::begin(); assert(s_cdc.txTimeout == 0); reset();
    assert(!usb_bridge::hostAlive());
    assert(usb_bridge::beginFile("a", 1, 1, 1) == ErrorReason::NoHost);
    hello(false); usb_bridge::loop(); assert(!usb_bridge::hostAlive());
    hello(true, 2); usb_bridge::loop(); assert(!usb_bridge::hostAlive());
    hello(); usb_bridge::loop(); assert(usb_bridge::hostAlive() && helloAcks == 2);
    // Arquivo com tamanho que não é múltiplo do chunk e escritas USB parciais.
    std::vector<uint8_t> content(20001);
    for (size_t i = 0; i < content.size(); ++i) content[i] = i;
    assert(usb_bridge::beginFile("ação teste.bin", content.size(), 1, 2) == ErrorReason::None);
    assert(usb_bridge::beginFile("outro", 0, 1, 1) == ErrorReason::Busy);
    assert(usb_bridge::writeChunk(content.data(), content.size()) == ErrorReason::None);
    assert(usb_bridge::endFile() == ErrorReason::None);
    assert(ends == 1 && dataFrames == 5 && peerCrc == protocol::crc32(0, content.data(), content.size()));
    // Arquivo vazio e próximo arquivo não reutilizam o id anterior.
    const uint32_t previousId = peerId;
    assert(usb_bridge::beginFile("vazio", 0, 2, 2) == ErrorReason::None && peerId != previousId);
    assert(usb_bridge::endFile() == ErrorReason::None);
    // Opcional: fixture para decodificar com o parser TypeScript real.
    if (argc == 2) {
        FILE *out = fopen(argv[1], "wb"); assert(out);
        assert(fwrite(transcript.data(), 1, transcript.size(), out) == transcript.size());
        fclose(out);
    }
    reset(); hello(); usb_bridge::loop(); autoAck = false;
    assert(usb_bridge::beginFile("janela", 5 * 4096, 1, 1) == ErrorReason::None);
    std::vector<uint8_t> block(4096);
    for (unsigned i = 0; i < 3; ++i) assert(usb_bridge::writeChunk(block.data(), block.size()) == ErrorReason::None);
    assert(waiting.size() == 3 && s_file.outstanding == 3);
    // ACK duplicado ou de outro id não libera crédito.
    ack(0); ack(12288, peerId + 1); usb_bridge::loop(); assert(s_file.outstanding == 3);
    fakeDelay = [] {
        if (s_file.outstanding < 4) return;
        assert(dataFrames == 4 && s_file.outstanding == 4);
        ack(waiting.back()); waiting.clear();
    };
    assert(usb_bridge::writeChunk(block.data(), block.size()) == ErrorReason::None);
    fakeDelay = [] { if (!waiting.empty()) { ack(waiting.back()); waiting.clear(); } };
    assert(usb_bridge::writeChunk(block.data(), block.size()) == ErrorReason::None);
    assert(usb_bridge::endFile() == ErrorReason::None);
    reset(); hello(); usb_bridge::loop(); rejectStart = true;
    assert(usb_bridge::beginFile("negado", 0, 1, 1) == ErrorReason::DiskError);
    assert(aborts == 1); usb_bridge::abortFile(ErrorReason::Aborted); assert(aborts == 1);
    reset(); hello(); usb_bridge::loop(); wrongDone = true;
    assert(usb_bridge::beginFile("crc", 0, 1, 1) == ErrorReason::None);
    assert(usb_bridge::endFile() == ErrorReason::Crc);
    reset(); hello(); usb_bridge::loop(); autoDone = false; fakeDelay = keepAlive;
    assert(usb_bridge::beginFile("sem done", 0, 1, 1) == ErrorReason::None);
    assert(usb_bridge::endFile() == ErrorReason::HostGone && fakeNow >= 30000);
    reset(); hello(); usb_bridge::loop(); autoStartAck = false; fakeDelay = keepAlive;
    assert(usb_bridge::beginFile("sem ack inicial", 0, 1, 1) == ErrorReason::HostGone && fakeNow >= 15000);
    reset(); hello(); usb_bridge::loop();
    assert(usb_bridge::beginFile("sem heartbeat", 0, 1, 1) == ErrorReason::None);
    autoDone = false;
    assert(usb_bridge::endFile() == ErrorReason::HostGone && fakeNow >= 3500 && fakeNow < 15000);
    reset(); hello(); usb_bridge::loop();
    assert(usb_bridge::beginFile("offset", 4096, 1, 1) == ErrorReason::None);
    ack(9999); assert(usb_bridge::writeChunk(block.data(), block.size()) == ErrorReason::Crc);
    reset(); hello(); usb_bridge::loop();
    assert(usb_bridge::beginFile("desplugado", 1, 1, 1) == ErrorReason::None);
    s_cdc.connected = false;
    assert(usb_bridge::writeChunk(block.data(), 1) == ErrorReason::HostGone);
    reset(); hello(); usb_bridge::loop(); fakeNow += 3500;
    assert(!usb_bridge::hostAlive());
    // FIFO inicialmente cheio e escritas menores que o espaço anunciado.
    reset(); hello(); usb_bridge::loop();
    s_cdc.readyAt = millis() + 40; s_cdc.burst = 17;
    assert(usb_bridge::beginFile("fifo", block.size(), 1, 1) == ErrorReason::None);
    assert(usb_bridge::writeChunk(block.data(), block.size()) == ErrorReason::None);
    assert(usb_bridge::endFile() == ErrorReason::None && s_cdc.largestWrite <= 64);
    // Ausência de crédito não pode descartar bytes nem virar sucesso.
    reset(); hello(); usb_bridge::loop(); s_cdc.capacity = 0;
    assert(usb_bridge::beginFile("fifo parado", 0, 1, 1) == ErrorReason::HostGone);
    assert(starts == 0 && s_file.phase == Phase::Idle && fakeNow >= 1500);
    // Mesmo tamanho do JPEG observado; fila de 64 B e HELLO durante o envio.
    reset(); hello(); usb_bridge::loop(); fakeDelay = keepAlive;
    std::vector<uint8_t> large(4336521);
    for (size_t i = 0; i < large.size(); ++i)
        large[i] = ((i * 1664525ULL + 1013904223ULL) ^ (i >> 7)) & 255;
    assert(usb_bridge::beginFile("grande.bin", large.size(), 1, 1) == ErrorReason::None);
    assert(usb_bridge::writeChunk(large.data(), large.size()) == ErrorReason::None);
    assert(usb_bridge::endFile() == ErrorReason::None);
    assert(peerReceived == large.size() && dataFrames == 1059 && s_cdc.largestWrite <= 64);
}
