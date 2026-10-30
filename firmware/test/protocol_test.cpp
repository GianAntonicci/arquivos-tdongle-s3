// Testes nativos dos vetores, fragmentação e ressincronização.
#include "protocol.h"
#include <assert.h>
#include <string.h>
#include <vector>
using namespace protocol;
static unsigned frames = 0, corrupt = 0;
static void receive(Type type, const uint8_t *p, size_t len) {
    assert(type == Type::Ack && len == 12);
    assert(read32(p) == 7 && read64(p + 4) == 4096);
    ++frames;
}
static void bad() { ++corrupt; }
int main() {
    const auto *text = reinterpret_cast<const uint8_t *>("123456789");
    assert(crc32(0, text, 9) == 0xCBF43926);
    assert(crc32(crc32(0, text, 4), text + 4, 5) == 0xCBF43926);
    uint8_t payload[12] = {}, frame[22];
    put32(payload, 7); put64(payload + 4, 4096);
    assert(encode(Type::Ack, payload, 12, frame, sizeof(frame)) == 22);
    const uint8_t expected[] = {0xa5,0x5a,0x11,0,0x0c,0,7,0,0,0,0,0x10,0,0,0,0,0,0,0x36,0,0x47,0x9d};
    assert(memcmp(frame, expected, sizeof(frame)) == 0);
    const uint8_t helloPayload[] = {1,1};
    uint8_t hello[12];
    const uint8_t expectedHello[] = {0xa5,0x5a,0x10,0,2,0,1,1,0x64,0x6a,1,0x76};
    assert(encode(Type::Hello, helloPayload, 2, hello, sizeof(hello)) == 12);
    assert(memcmp(hello, expectedHello, 12) == 0);
    Parser parser(receive, bad);
    for (size_t split = 0; split <= sizeof(frame); ++split) {
        parser.push(frame, split);
        parser.push(frame + split, sizeof(frame) - split);
    }
    assert(frames == 23);
    // Ruído, magic parcial, tamanho inválido, CRC errado e frame válido junto.
    std::vector<uint8_t> stream = {0,0xa5,0xa5,0x5a,0x11,0,0xff,0xff};
    stream.insert(stream.end(), frame, frame + sizeof(frame));
    stream.back() ^= 1;
    stream.insert(stream.end(), frame, frame + sizeof(frame));
    parser.push(stream.data(), stream.size());
    assert(corrupt >= 2 && frames == 24);
    parser.push(frame, 7); parser.reset();
    parser.push(frame, sizeof(frame));
    assert(frames == 25);
    uint8_t integer[8];
    put64(integer, 0xFEDCBA9876543210ULL);
    assert(read64(integer) == 0xFEDCBA9876543210ULL);
    assert(encode(Type::Ack, payload, 12, frame, 21) == 0);
    // O limite do payload precisa caber e o parser deve continuar após ele.
    std::vector<uint8_t> large(MAX_PAYLOAD + HEADER + TRAILER);
    assert(encode(Type::Log, large.data() + HEADER, MAX_PAYLOAD, large.data(), large.size()) == large.size());
    Parser ignore([](Type, const uint8_t *, size_t) { ++frames; }, bad);
    for (uint8_t byte : large) ignore.push(&byte, 1);
    assert(frames == 26);
}
