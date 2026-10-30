#include "protocol.h"
#include <string.h>
#ifdef ARDUINO
#include <esp_rom_crc.h>
#endif

namespace protocol {
uint16_t read16(const uint8_t *p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t read32(const uint8_t *p) {
    return uint32_t(read16(p)) | (uint32_t(read16(p + 2)) << 16);
}
uint64_t read64(const uint8_t *p) {
    return uint64_t(read32(p)) | (uint64_t(read32(p + 4)) << 32);
}
void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
void put64(uint8_t *p, uint64_t v) { put32(p, v); put32(p + 4, v >> 32); }
uint32_t crc32(uint32_t previous, const uint8_t *data, size_t len) {
#ifdef ARDUINO
    return esp_rom_crc32_le(previous, data, len);
#else
    // Referência portátil para os testes nativos dos vetores do protocolo.
    uint32_t crc = ~previous;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320U : 0);
    }
    return ~crc;
#endif
}
size_t encode(Type type, const uint8_t *payload, size_t len, uint8_t *out, size_t capacity) {
    const size_t total = HEADER + len + TRAILER;
    if (len > MAX_PAYLOAD || total > capacity) return 0;
    if (len) memmove(out + HEADER, payload, len);
    out[0] = 0xA5; out[1] = 0x5A;
    out[2] = static_cast<uint8_t>(type); out[3] = 0;
    put16(out + 4, len);
    put32(out + HEADER + len, crc32(0, out + 2, 4 + len));
    return total;
}
void Parser::discard(size_t len) {
    m_used -= len;
    if (m_used) memmove(m_buffer, m_buffer + len, m_used);
}
void Parser::parse() {
    while (m_used >= HEADER) {
        if (m_buffer[0] != 0xA5 || m_buffer[1] != 0x5A) { discard(1); continue; }
        const size_t len = read16(m_buffer + 4);
        if (len > MAX_PAYLOAD) { m_corrupt(); discard(1); continue; }
        const size_t total = HEADER + len + TRAILER;
        if (m_used < total) return;
        if (read32(m_buffer + HEADER + len) != crc32(0, m_buffer + 2, 4 + len)) {
            m_corrupt(); discard(1); continue;
        }
        // O callback consome o payload antes de reutilizarmos este buffer.
        if (m_buffer[3] == 0) m_handler(static_cast<Type>(m_buffer[2]), m_buffer + HEADER, len);
        discard(total);
    }
}
void Parser::push(const uint8_t *data, size_t len) {
    while (len) {
        size_t take = sizeof(m_buffer) - m_used;
        if (take > len) take = len;
        memcpy(m_buffer + m_used, data, take);
        m_used += take; data += take; len -= take;
        parse();
    }
}
}  // namespace protocol
