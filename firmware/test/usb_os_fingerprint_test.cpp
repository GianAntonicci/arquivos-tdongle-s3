// Teste nativo: c++ -std=c++11 -Wall -Wextra -Werror -Iinclude
// test/usb_os_fingerprint_test.cpp -o /tmp/tdongle-os-test && /tmp/tdongle-os-test
#include <assert.h>
#include <initializer_list>
#include "usb_os_fingerprint.h"

static TargetOS classify(std::initializer_list<uint16_t> lengths) {
    UsbOsFingerprint fingerprint;
    for (uint16_t length : lengths) fingerprint.observe(length);
    return fingerprint.detected();
}

int main() {
    assert(classify({}) == TargetOS::Auto);
    assert(classify({255, 255}) == TargetOS::Auto);
    assert(classify({255, 255, 255}) == TargetOS::Linux);
    assert(classify({255, 255, 4}) == TargetOS::Windows);
    assert(classify({2, 4, 2, 20, 255}) == TargetOS::MacOS);
    // iOS e sequências incompletas não devem lançar atalhos de desktop.
    assert(classify({2, 4, 2, 20}) == TargetOS::Auto);
    assert(classify({18, 64, 9}) == TargetOS::Auto);
    UsbOsFingerprint fingerprint;
    for (unsigned i = 0; i < 100000; ++i) fingerprint.observe(255);
    assert(fingerprint.detected() == TargetOS::Linux);
    fingerprint = {};
    assert(fingerprint.detected() == TargetOS::Auto);
    fingerprint.observe(4);
    fingerprint.observe(255);
    fingerprint.observe(255);
    assert(fingerprint.detected() == TargetOS::Windows);
}
