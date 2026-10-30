#include "usb_bridge.h"

#include <Arduino.h>
#include <USB.h>
#include <USBCDC.h>

#include "config.h"

// CDC própria (CDC_ON_BOOT=0): criada antes de USB.begin() junto com o HID
static USBCDC s_cdc(0);

static volatile bool s_mounted = false;
static volatile bool s_mountEvent = false;

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

void loop() {}  // etapa (d): HELLO/ACK do protocolo

// ---------------------------------------------------------------------------
// Etapa (c): stubs. Com BRIDGE_NULL_SINK=1 os dados são aceitos e descartados,
// para medir a velocidade do Wi-Fi sem o PC. Etapa (d) troca pelo protocolo.
// ---------------------------------------------------------------------------
#if BRIDGE_NULL_SINK
bool hostAlive() { return true; }
ErrorReason beginFile(const char *, uint64_t, uint16_t, uint16_t) { return ErrorReason::None; }
ErrorReason writeChunk(const uint8_t *, size_t) { return ErrorReason::None; }
ErrorReason endFile() { return ErrorReason::None; }
#else
bool hostAlive() { return false; }
ErrorReason beginFile(const char *, uint64_t, uint16_t, uint16_t) { return ErrorReason::NoHost; }
ErrorReason writeChunk(const uint8_t *, size_t) { return ErrorReason::NoHost; }
ErrorReason endFile() { return ErrorReason::NoHost; }
#endif
void abortFile(ErrorReason) {}

}  // namespace usb_bridge
