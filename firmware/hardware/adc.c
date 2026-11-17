#include "scope_internal.h"

static uint32_t adc_step_started_ticks;

void adc_poll(void) {
    if (scope_state.adc.state != SETUP_RUNNING) {
        return;
    }
    if (scope_state.adc.wait &&
        adc_step_started_ticks - timer_ticks() < scope_state.adc.wait * 2400u) {
        return;
    }
    unsigned total = scope_state.adc.mode ? sizeof(adc_normal_setup) / sizeof(adc_normal_setup[0])
                                          : sizeof(adc_setup) / sizeof(adc_setup[0]);
    if (scope_state.adc.step_index == total) {
        scope_state.adc.state = SETUP_COMPLETE;
        return;
    }
    const unsigned *event = scope_state.adc.mode ? adc_normal_setup[scope_state.adc.step_index++]
                                                 : adc_setup[scope_state.adc.step_index++];
    if (event[0]) {
        scope_state.adc.wait = event[2];
    } else {
        unsigned value = event[2];
        if (!scope_state.adc.mode && event[1] == 0x88 && ((value >> 16) & 255) == 0x29) {
            value = (value & ~0x70u) | (scope_factory_phase(0) << 4);
            if (value & 0x1000000u) {
                value = (value & ~0xeu) | (scope_factory_phase(1) << 1);
            }
        }
        if (fpga_write_register(event[1], value)) {
            scope_state.adc.error = 1;
            scope_state.adc.state = SETUP_FAILED;
            return;
        }
        scope_state.adc.wait = 1;
    }
    adc_step_started_ticks = timer_ticks();
}
