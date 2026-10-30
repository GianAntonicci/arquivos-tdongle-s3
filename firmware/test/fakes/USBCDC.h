#pragma once
#include <deque>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include "Arduino.h"
struct USBCDC {
    std::deque<uint8_t> rx;
    bool connected = true;
    size_t burst = 4096;
    size_t capacity = 64;
    uint32_t readyAt = 0;
    uint32_t txTimeout = 0;
    size_t largestWrite = 0;
    void (*peer)(const uint8_t *, size_t) = nullptr;
    explicit USBCDC(int) {}
    void setRxBufferSize(size_t) {}
    void setTxTimeoutMs(uint32_t value) { txTimeout = value; }
    void enableReboot(bool) {}
    void begin() {}
    explicit operator bool() const { return connected; }
    int available() { return static_cast<int>(rx.size()); }
    int availableForWrite() {
        return connected && static_cast<int32_t>(millis() - readyAt) >= 0 ? static_cast<int>(capacity) : 0;
    }
    size_t read(uint8_t *out, size_t len) {
        size_t used = 0;
        while (used < len && !rx.empty()) { out[used++] = rx.front(); rx.pop_front(); }
        return used;
    }
    size_t write(const uint8_t *data, size_t len) {
        if (!connected) return 0;
        // Falha se a ponte ignorar o crédito ou escrever de novo antes do TX.
        assert(len <= static_cast<size_t>(availableForWrite()));
        if (len > largestWrite) largestWrite = len;
        const size_t n = len < burst ? len : burst;
        if (peer && n) peer(data, n);
        if (n) readyAt = millis() + 1;
        return n;
    }
};
