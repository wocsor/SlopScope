#include "front_panel.h"
#include "panel_tools.h"
#include "scope_internal.h"

static unsigned autoset_state, autoset_channel, autoset_range, autoset_rate;
static unsigned trigger_candidate, trigger_sequence, trigger_wait_ms, previous_save_hold;
static uint32_t trigger_started_ms;
static unsigned preview_sequence, range_refinements, candidate_wait_started, minimum_range;

static unsigned autoset_active(void) {
    return (autoset_state >= PANEL_AUTOSET_POSITION && autoset_state <= PANEL_AUTOSET_TRIGGER) ||
           autoset_state == PANEL_AUTOSET_PREVIEW;
}

static unsigned autoset_choose_range(struct scope_signal_summary signal, unsigned current_range) {
    struct scope_voltage_cal calibration;
    scope_get_voltage_cal(autoset_channel, current_range, &calibration);
    unsigned wanted_mv =
        scope_ratio((int64_t)signal.amplitude * calibration.gain, 256u * 4u * 1000u);
    unsigned offset = (scope_state.frame.words[13] >> (autoset_channel * 16)) & 65535;
    int64_t minimum_uv = scope_voltage_uv(&calibration, signal.minimum_code, offset);
    int64_t maximum_uv = scope_voltage_uv(&calibration, signal.maximum_code, offset);
    if (minimum_uv < 0) {
        minimum_uv = -minimum_uv;
    }
    if (maximum_uv < 0) {
        maximum_uv = -maximum_uv;
    }
    unsigned headroom_mv = scope_ratio(minimum_uv > maximum_uv ? minimum_uv : maximum_uv, 3000);
    if (wanted_mv < headroom_mv) {
        wanted_mv = headroom_mv;
    }
    unsigned selected = scope_voltage_baseline() == 1 ? 2 : 3;
    uint64_t best_distance = UINT64_MAX;
    for (unsigned range = selected; range < 12; range++) {
        unsigned scale_mv = scope_scale_mv(range);
        unsigned minimum = scale_mv < wanted_mv ? scale_mv : wanted_mv;
        unsigned maximum = scale_mv > wanted_mv ? scale_mv : wanted_mv;
        uint64_t distance = scope_ratio((int64_t)maximum * 1000000, minimum ? minimum : 1);
        if (distance < best_distance) {
            best_distance = distance;
            selected = range;
        }
    }
    if (signal.clipped && selected <= current_range) {
        selected = current_range < 11 ? current_range + 1 : 11;
    }
    if (signal.clipped && current_range < 11) {
        minimum_range = current_range + 1;
    }
    if (selected < minimum_range) {
        selected = minimum_range;
    }
    return selected;
}

static void autoset_finish(unsigned state) {
    scope_state.settings.save_held = previous_save_hold;
    autoset_state = state;
    if (!previous_save_hold) {
        scope_state.settings.dirty = 1;
        scope_state.settings.changed = timer_milliseconds();
        scope_state.settings.save_delay = 2000;
    }
    front_panel_menu_refresh();
}

unsigned panel_autoset_state(void) {
    return autoset_state;
}

void panel_autoset_cancel(void) {
    if (autoset_active()) {
        front_panel_issue("LIVE OFF");
        autoset_finish(PANEL_AUTOSET_CANCELLED);
        front_panel_issue("TRIGGER SWEEP AUTO");
        front_panel_issue("LIVE ON");
    }
}

__attribute__((section(".panel_text"), noinline)) void panel_autoset_start(void) {
    panel_autoset_cancel();
    if (scope_state.boot.stage != BOOT_ACQUIRING || !scope_state.frame.sequence ||
        scope_state.analog.state == SETUP_RUNNING || scope_state.adc.state != SETUP_COMPLETE ||
        scope_state.settings.restore_state == 1 || scope_state.record.active) {
        autoset_state = PANEL_AUTOSET_FAILED;
        front_panel_menu_refresh();
        return;
    }
    struct scope_signal_summary signals[2] = {scope_summarize_channel(0),
                                              scope_summarize_channel(1)};
    int64_t strongest_uv = -1;
    unsigned periodic_source = 0;
    autoset_channel = 0;
    for (unsigned channel = 0; channel < 2; channel++) {
        if (!scope_state.channels.visible[channel] || !signals[channel].count ||
            signals[channel].amplitude < 12) {
            continue;
        }
        struct scope_voltage_cal calibration;
        scope_get_voltage_cal(channel, scope_state.frame.words[8 + channel], &calibration);
        int64_t amplitude_uv = scope_ratio((int64_t)signals[channel].amplitude * calibration.gain *
                                               scope_state.channels.probe_multiplier[channel],
                                           256);
        unsigned periodic = signals[channel].period_samples != 0;
        if (strongest_uv < 0 || periodic > periodic_source ||
            (periodic == periodic_source && amplitude_uv > strongest_uv)) {
            autoset_channel = channel;
            strongest_uv = amplitude_uv;
            periodic_source = periodic;
        }
    }
    struct scope_signal_summary signal = signals[autoset_channel];
    if (strongest_uv < 0) {
        autoset_state = PANEL_AUTOSET_NO_SIGNAL;
        front_panel_menu_refresh();
        return;
    }
    unsigned current_range = scope_state.frame.words[8 + autoset_channel];
    minimum_range = scope_voltage_baseline() == 1 ? 2 : 3;
    autoset_range = autoset_choose_range(signal, current_range);
    uint64_t best_distance = UINT64_MAX;
    autoset_rate = scope_state.frame.words[14];
    trigger_wait_ms = 200;
    if (signal.period_samples) {
        unsigned source_rate = scope_state.frame.words[14];
        unsigned frequency_hz =
            scope_ratio(sample_rate_numerator(source_rate),
                        sample_rate_denominator(source_rate) * signal.period_samples);
        uint64_t desired_hz = scope_ratio((int64_t)frequency_hz * 750, 4);
        unsigned wanted_hz = desired_hz > 500000000 ? 500000000 : desired_hz;
        if (wanted_hz) {
            best_distance = UINT64_MAX;
            for (unsigned rate = 1; rate < 16; rate++) {
                unsigned hz = sample_rate_numerator(rate);
                unsigned minimum = hz < wanted_hz ? hz : wanted_hz;
                unsigned maximum = hz > wanted_hz ? hz : wanted_hz;
                uint64_t distance = scope_ratio((int64_t)maximum * 1000000, minimum);
                if (distance < best_distance) {
                    best_distance = distance;
                    autoset_rate = rate;
                }
            }
            trigger_wait_ms = frequency_hz ? 3000 / frequency_hz : 500;
            if (trigger_wait_ms < 200) {
                trigger_wait_ms = 200;
            } else if (trigger_wait_ms > 500) {
                trigger_wait_ms = 500;
            }
        }
    }
    previous_save_hold = scope_state.settings.save_held;
    range_refinements = 0;
    scope_state.settings.save_held = 1;
    front_panel_issue("LIVE OFF");
    if (!front_panel_issue_value(autoset_channel ? "CH2 POSITION " : "CH1 POSITION ", 0)) {
        autoset_finish(PANEL_AUTOSET_FAILED);
        return;
    }
    autoset_state = PANEL_AUTOSET_POSITION;
    front_panel_menu_refresh();
}

static int autoset_begin_candidate(void) {
    int code = trigger_candidate ? ((trigger_candidate + 1) / 2) * 16 : 0;
    if (trigger_candidate & 1) {
        code = -code;
    }
    front_panel_issue("LIVE OFF");
    if (!front_panel_issue_value("TRIGGER CODE ", code)) {
        return 0;
    }
    trigger_sequence = scope_state.frame.sequence;
    candidate_wait_started = 0;
    return front_panel_issue("LIVE ON");
}

void panel_autoset_poll(void) {
    if (!autoset_active() || scope_state.analog.state == SETUP_RUNNING) {
        return;
    }
    if (scope_state.analog.state == SETUP_FAILED || scope_state.capture.state == CAPTURE_FAILED) {
        autoset_finish(PANEL_AUTOSET_FAILED);
        return;
    }
    if (autoset_state == PANEL_AUTOSET_POSITION) {
        if (!front_panel_issue_value(autoset_channel ? "CH2 RANGE " : "CH1 RANGE ",
                                     autoset_range)) {
            autoset_finish(PANEL_AUTOSET_FAILED);
            return;
        }
        autoset_state = PANEL_AUTOSET_RANGE;
    } else if (autoset_state == PANEL_AUTOSET_RANGE) {
        int ready =
            front_panel_issue("TRIGGER FAMILY NORMAL") && front_panel_issue("TRIGGER MODE EDGE") &&
            front_panel_issue("ACQUIRE MODE SAMPLE") && front_panel_issue("HORIZONTAL STOCK") &&
            front_panel_issue_value("CAPTURE RATE ", autoset_rate) &&
            front_panel_issue("VIEW WINDOW 0 750") && front_panel_issue("TRIGGER ENABLE ON") &&
            front_panel_issue(autoset_channel ? "TRIGGER SOURCE CH2" : "TRIGGER SOURCE CH1") &&
            front_panel_issue("TRIGGER SLOPE RISE") && front_panel_issue("TRIGGER SWEEP AUTO") &&
            front_panel_issue("TRIGGER CODE 0");
        preview_sequence = scope_state.frame.sequence;
        trigger_started_ms = timer_milliseconds();
        if (!ready || !front_panel_issue("LIVE ON")) {
            autoset_finish(PANEL_AUTOSET_FAILED);
            return;
        }
        autoset_state = PANEL_AUTOSET_PREVIEW;
    } else if (autoset_state == PANEL_AUTOSET_PREVIEW) {
        if (scope_state.frame.sequence == preview_sequence) {
            if (timer_elapsed_ms(trigger_started_ms) > 3000) {
                autoset_finish(PANEL_AUTOSET_FAILED);
            }
            return;
        }
        struct scope_signal_summary signal = scope_summarize_channel(autoset_channel);
        if (!signal.count) {
            return;
        }
        if (signal.clipped && autoset_range == 11) {
            autoset_finish(PANEL_AUTOSET_CLIPPED);
            return;
        }
        unsigned selected = autoset_choose_range(signal, autoset_range);
        if (signal.clipped || (selected != autoset_range && range_refinements++ < 2)) {
            front_panel_issue("LIVE OFF");
            autoset_range = selected;
            if (!front_panel_issue_value(autoset_channel ? "CH2 RANGE " : "CH1 RANGE ", selected)) {
                autoset_finish(PANEL_AUTOSET_FAILED);
                return;
            }
            autoset_state = PANEL_AUTOSET_RANGE;
            return;
        }
        if (signal.amplitude < 12) {
            autoset_finish(PANEL_AUTOSET_NO_SIGNAL);
            return;
        }
        front_panel_issue("LIVE OFF");
        front_panel_issue("TRIGGER SWEEP NORMAL");
        trigger_candidate = 0;
        if (!autoset_begin_candidate()) {
            autoset_finish(PANEL_AUTOSET_FAILED);
            return;
        }
        autoset_state = PANEL_AUTOSET_TRIGGER;
    } else if (scope_state.frame.sequence != trigger_sequence &&
               !scope_state.frame.extra_fields[5]) {
        front_panel_issue("LIVE OFF");
        front_panel_issue("TRIGGER SWEEP AUTO");
        autoset_finish(PANEL_AUTOSET_COMPLETE);
        front_panel_issue("LIVE ON");
    } else if (!candidate_wait_started) {
        if (scope_state.capture.state == CAPTURE_WAITING ||
            scope_state.capture.state == CAPTURE_TRANSFERRING) {
            trigger_started_ms = timer_milliseconds();
            candidate_wait_started = 1;
        }
    } else if (timer_elapsed_ms(trigger_started_ms) >= trigger_wait_ms) {
        if (++trigger_candidate < 15) {
            if (!autoset_begin_candidate()) {
                autoset_finish(PANEL_AUTOSET_FAILED);
            }
        } else {
            front_panel_issue("LIVE OFF");
            front_panel_issue("TRIGGER SWEEP AUTO");
            autoset_finish(PANEL_AUTOSET_NO_TRIGGER);
            front_panel_issue("LIVE ON");
        }
    }
}

unsigned panel_autoset_command(const uint8_t *command, unsigned length, uint8_t *reply) {
    if (command_equals(command, length, "AUTOSET START")) {
        panel_autoset_start();
        return reply_write_text(reply, "OK autoset requested\n");
    }
    if (command_equals(command, length, "AUTOSET CANCEL")) {
        panel_autoset_cancel();
        return reply_write_text(reply, "OK autoset cancelled\n");
    }
    if (!command_equals(command, length, "AUTOSET STATUS")) {
        return 0;
    }
    unsigned reply_length = reply_write_text(reply, "AUTOSET state=");
    reply_length += reply_write_hex32(reply + reply_length, autoset_state);
    reply_length += reply_write_text(reply + reply_length, " source=");
    reply_length += reply_write_hex32(reply + reply_length, autoset_channel);
    reply_length += reply_write_text(reply + reply_length, " range=");
    reply_length += reply_write_hex32(reply + reply_length, autoset_range);
    reply_length += reply_write_text(reply + reply_length, " rate=");
    reply_length += reply_write_hex32(reply + reply_length, autoset_rate);
    reply[reply_length++] = '\n';
    return reply_length;
}
