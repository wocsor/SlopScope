#include "scope_internal.h"

static uint32_t analog_step_started_ticks;

void analog_poll(void) {
    if (scope_state.analog.state != SETUP_RUNNING || capture_busy()) {
        return;
    }
    if (scope_state.analog.wait_ticks &&
        analog_step_started_ticks - timer_ticks() < scope_state.analog.wait_ticks) {
        return;
    }
    unsigned init_count = sizeof(analog_setup) / sizeof(analog_setup[0]),
             range_count =
                 channel_plan_counts[scope_state.analog.job_channel][scope_state.analog.job_range];
    unsigned range_operation = scope_state.analog.job_kind == 1 || scope_state.analog.job_kind == 4;
    unsigned total = scope_state.analog.job_kind == 0 ? init_count + 2
                     : range_operation                ? range_count + 1
                                                      : 1;
    if (scope_state.analog.step_index == total) {
        scope_state.analog.state = SETUP_COMPLETE;
        scope_state.analog.configured = 1;
        scope_state.display.display_redraw_requested = 1;
        if (!scope_state.analog.job_kind) {
            scope_state.channels.range[0] = scope_state.channels.range[1] = 9;
            scope_state.channels.coupling[0] = scope_state.channels.coupling[1] = 0;
            scope_position_dac(0, 9, 0, &scope_state.channels.offset_dac[0]);
            scope_position_dac(1, 9, 0, &scope_state.channels.offset_dac[1]);
            scope_state.channels.position_millidivisions[0] =
                scope_state.channels.position_millidivisions[1] = 0;
            scope_voltage_position(0, 0);
            scope_voltage_position(1, 0);
            scope_state.configuration.features.bandwidth[0] =
                scope_state.configuration.features.bandwidth[1] = 0;
            scope_state.analog.shared_register_shadow = 0;
        } else {
            scope_state.channels.range[scope_state.analog.job_channel] =
                scope_state.analog.job_range;
            scope_state.channels.coupling[scope_state.analog.job_channel] =
                scope_state.analog.job_coupling;
            scope_state.channels.offset_dac[scope_state.analog.job_channel] =
                scope_state.analog.job_offset;
            scope_state.channels.position_millidivisions[scope_state.analog.job_channel] =
                scope_state.analog.job_position;
            scope_voltage_position(scope_state.analog.job_channel, scope_state.analog.job_position);
            if (range_operation) {
                scope_state.analog.shared_register_shadow = scope_state.analog.range_job.shadow;
                scope_state.configuration.features.bandwidth[scope_state.analog.job_channel] =
                    scope_state.analog.job_bandwidth;
            }
        }
        return;
    }
    /* Stock 1fffc: B4/B8 are input coupling (DC=1, AC=0) */
    unsigned coupling_value = scope_state.analog.job_coupling == 0   ? 1
                              : scope_state.analog.job_coupling == 1 ? 0
                                                                     : 2;
    unsigned coupling_event[3] = {0, scope_state.analog.job_channel ? 0xb8 : 0xb4, coupling_value};
    unsigned init_event[3] = {0, scope_state.analog.step_index == init_count ? 0xb4 : 0xb8, 1};
    unsigned direct[3] = {
        0,
        scope_state.analog.job_kind == 2 ? (scope_state.analog.job_channel ? 0xb8 : 0xb4)
                                         : (scope_state.analog.job_channel ? 0x4ec : 0x4e8),
        scope_state.analog.job_kind == 2 ? coupling_value : scope_state.analog.job_offset};
    const unsigned *event =
        scope_state.analog.job_kind == 0 ? (scope_state.analog.step_index < init_count
                                                ? analog_setup[scope_state.analog.step_index]
                                                : init_event)
        : range_operation
            ? (scope_state.analog.step_index < range_count
                   ? channel_plans[scope_state.analog.job_channel][scope_state.analog.job_range]
                                  [scope_state.analog.step_index]
                   : coupling_event)
            : direct;
    scope_state.analog.step_index++;
    if (event[0]) {
        /* These trace delays are stock busy-loop units; use a conservative 100 us. */
        scope_state.analog.wait_ticks = event[2] * 2400u;
    } else {
        unsigned value = event[2];
        if (range_operation) {
            if (event[1] == 0xc4 || event[1] == 0xc8) {
                value = scope_state.analog.job_bandwidth;
            }
            if (event[1] == 0x41c) {
                value = scope_state.analog.range_job.shadow;
            }
            if (event[1] == 0x4e8 || event[1] == 0x4ec) {
                {
                    unsigned positioned;
                    if (!scope_effective_position_dac(scope_state.analog.job_channel,
                                                      scope_state.analog.job_range,
                                                      scope_state.analog.job_position,
                                                      &positioned)) {
                        scope_state.analog.error = 2;
                        scope_state.analog.state = SETUP_FAILED;
                        scope_state.capture.live_enabled = 0;
                        return;
                    }
                    value = positioned;
                }
                scope_state.analog.job_offset = value;
            }
        }
        /* Only the paired calibration state chooses ADC gain/trim */
        if (range_operation && event[1] == 0x88) {
            value = scope_adc_gain_command(
                scope_state.analog.job_channel, scope_state.analog.job_range, value);
        }
        if (!scope_state.analog.job_kind && (event[1] == 0x4e8 || event[1] == 0x4ec)) {
            unsigned offset_dac;
            if (scope_position_dac(event[1] == 0x4ec, 9, 0, &offset_dac)) {
                value = offset_dac;
            }
        }
        if (fpga_write_register(event[1], value)) {
            scope_state.analog.error = 1;
            scope_state.analog.state = SETUP_FAILED;
            scope_state.analog.configured = scope_state.capture.live_enabled = 0;
            return;
        }
        /* Allow mechanical analog switching to settle without blocking USB. */
        scope_state.analog.wait_ticks =
            (event[1] == 0x4f0 || event[1] == 0xc4 || event[1] == 0xc8 || event[1] == 0xb4 ||
             event[1] == 0xb8 || event[1] == 0x4e8 || event[1] == 0x4ec)
                ? 250u * 24000u
                : 2400u;
    }
    analog_step_started_ticks = timer_ticks();
}
