#pragma once
// Espelho de web/src/protocol.ts; inteiros little-endian, CRC32 IEEE/zlib.
#include <stddef.h>
#include <stdint.h>

namespace protocol {
constexpr uint8_t VERSION = 1;
constexpr size_t MAX_PAYLOAD = 8192;
constexpr size_t HEADER = 6;
constexpr size_t TRAILER = 4;
enum class Type : uint8_t {
    Start = 0x01, Data, End, Abort, Log, HelloAck,
    Hello = 0x10, Ack, Nak, Done
};
uint16_t read16(const uint8_t *p);
uint32_t read32(const uint8_t *p);
uint64_t read64(const uint8_t *p);
void put16(uint8_t *p, uint16_t value);
void put32(uint8_t *p, uint32_t value);
void put64(uint8_t *p, uint64_t value);
uint32_t crc32(uint32_t previous, const uint8_t *data, size_t len);
size_t encode(Type type, const uint8_t *payload, size_t len, uint8_t *out, size_t capacity);

class Parser {
  public:
    using Handler = void (*)(Type, const uint8_t *, size_t);
    using Corrupt = void (*)();
    Parser(Handler handler, Corrupt corrupt) : m_handler(handler), m_corrupt(corrupt) {}
    void push(const uint8_t *data, size_t len);
    void reset() { m_used = 0; }
  private:
    uint8_t m_buffer[HEADER + MAX_PAYLOAD + TRAILER] = {};
    size_t m_used = 0;
    Handler m_handler;
    Corrupt m_corrupt;
    void parse();
    void discard(size_t len);
};
}  // namespace protocol
