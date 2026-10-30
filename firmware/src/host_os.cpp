#include "host_os.h"

#include <Arduino.h>
#include <tusb.h>
#include <device/dcd.h>

#include "settings.h"
#include "usb_os_fingerprint.h"

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static UsbOsFingerprint s_fingerprint;

// O driver chama este ponto tanto de interrupções como da task USB. Apenas
// contamos requisições; descritores e eventos seguem intactos para o TinyUSB.
extern "C" void __real_dcd_event_handler(const dcd_event_t *event, bool inIsr);
extern "C" void __wrap_dcd_event_handler(const dcd_event_t *event, bool inIsr) {
    if (event->event_id == DCD_EVENT_BUS_RESET || event->event_id == DCD_EVENT_UNPLUGGED) {
        portENTER_CRITICAL_SAFE(&s_mux);
        s_fingerprint = {};
        portEXIT_CRITICAL_SAFE(&s_mux);
    } else if (event->event_id == DCD_EVENT_SETUP_RECEIVED) {
        const auto &r = event->setup_received;
        if (r.bmRequestType == 0x80 && r.bRequest == TUSB_REQ_GET_DESCRIPTOR &&
            (r.wValue >> 8) == TUSB_DESC_STRING) {
            portENTER_CRITICAL_SAFE(&s_mux);
            s_fingerprint.observe(r.wLength);
            portEXIT_CRITICAL_SAFE(&s_mux);
        }
    }
    __real_dcd_event_handler(event, inIsr);
}

namespace host_os {

TargetOS detected() {
    portENTER_CRITICAL(&s_mux);
    const TargetOS os = s_fingerprint.detected();
    portEXIT_CRITICAL(&s_mux);
    return os;
}

TargetOS effective() {
    const TargetOS configured = settings::get().os;
    return configured == TargetOS::Auto ? detected() : configured;
}

}  // namespace host_os
