#include "scope_internal.h"

void scope_poll(void) {
    scope_lifecycle_poll();
    analog_poll();
    adc_poll();
    capture_poll();
    live_poll();
    display_poll();
    display_usb_activity_poll();
    activity_poll();
    fpga_configuration_poll();
}
