#include "front_panel.h"
#include "scope_internal.h"
#include "screenshot.h"
#include "string.h"

enum panel_register {
    PANEL_EVENT_READY = 0x20,
    PANEL_EVENT_CODE = 0x24,
    PANEL_ENCODER_STEPS = 0x28,
    PANEL_LED_CONTROL = 0x4d0
};

/* DOS1102 physical event numbers, decoded by stock V4 app offset 0x3a988. */
enum panel_event {
    PANEL_CH2_SCALE_UP = 1,
    PANEL_CH2_SCALE_DOWN,
    PANEL_CH1_SCALE_UP,
    PANEL_CH1_SCALE_DOWN,
    PANEL_CH1_POSITION_UP,
    PANEL_CH1_POSITION_DOWN,
    PANEL_HORIZONTAL_RIGHT,
    PANEL_HORIZONTAL_LEFT,
    PANEL_CH2_POSITION_UP,
    PANEL_CH2_POSITION_DOWN,
    PANEL_TRIGGER_DOWN,
    PANEL_TRIGGER_UP,
    PANEL_TIMEBASE_FASTER,
    PANEL_TIMEBASE_SLOWER,
    PANEL_GENERAL_COUNTERCLOCKWISE,
    PANEL_GENERAL_CLOCKWISE,
    PANEL_CH2_MENU,
    PANEL_CH1_MENU,
    PANEL_RIGHT_2,
    PANEL_RIGHT_3,
    PANEL_RIGHT_4,
    PANEL_RIGHT_5,
    PANEL_RIGHT_1,
    PANEL_FORCE,
    PANEL_TRIGGER_MENU,
    PANEL_TRIGGER_CENTER,
    PANEL_CH2_CENTER,
    PANEL_MATH,
    PANEL_CH1_CENTER,
    PANEL_HORIZONTAL_CENTER,
    PANEL_HORIZONTAL_MENU,
    PANEL_CURSOR_MENU,
    PANEL_ACQUIRE_MENU,
    PANEL_MEASURE_MENU,
    PANEL_RUN_STOP,
    PANEL_AUTOSET,
    PANEL_UTILITY_MENU,
    PANEL_GENERAL_SELECT,
    PANEL_COPY
};

struct panel_pending_event {
    unsigned code;
    unsigned steps;
};

static struct panel_pending_event pending_events[16];
static unsigned pending_head, pending_count;
static unsigned event_count, last_event, last_steps, dropped_events, command_errors;
static unsigned selected_channel, last_command_failed;
static uint32_t last_poll_ticks;
static unsigned last_led_value;
static uint8_t command_reply[CONTROL_REPLY_MAX];

int front_panel_issue(const char *command) {
    unsigned length = scope_command((const uint8_t *)command, strlen(command), command_reply);
    int success = length >= 3 && !memcmp(command_reply, "OK ", 3);
    last_command_failed = !success;
    if (!success) {
        command_errors++;
    }
    return success;
}

unsigned front_panel_command_failed(void) {
    return last_command_failed;
}

int front_panel_issue_value(const char *prefix, int32_t value) {
    char command[64], digits[20];
    unsigned length = strlen(prefix), digit_count = 0;
    memcpy(command, prefix, length);
    if (value < 0) {
        command[length++] = '-';
        value = -value;
    }
    do {
        digits[digit_count++] = '0' + value % 10;
        value /= 10;
    } while (value);
    while (digit_count) {
        command[length++] = digits[--digit_count];
    }
    command[length] = 0;
    return front_panel_issue(command);
}

static void panel_change_range(unsigned channel, int direction) {
    int range = (int)scope_state.channels.range[channel] + direction;
    unsigned minimum = scope_voltage_baseline() == 1 ? 0 : 3;
    unsigned maximum = scope_voltage_baseline() == 1 ? 13 : 11;
    if (range < (int)minimum) {
        range = minimum;
    } else if (range > (int)maximum) {
        range = maximum;
    }
    if (range != (int)scope_state.channels.range[channel]) {
        front_panel_issue_value(channel ? "CH2 RANGE " : "CH1 RANGE ", range);
    }
}

static void panel_change_position(unsigned channel, int direction) {
    int position = scope_state.channels.position_millidivisions[channel] + direction * 100;
    front_panel_issue_value(channel ? "CH2 POSITION " : "CH1 POSITION ", position);
}

static void panel_change_trigger(int direction) {
    unsigned channel = scope_state.trigger.configured.source;
    int64_t step_uv = (int64_t)scope_scale_mv(scope_state.channels.range[channel]) *
                      scope_state.channels.probe_multiplier[channel] * 100;
    int64_t level_uv = scope_state.trigger.configured.level_uv;
    if (scope_state.trigger.configured.use_code) {
        int64_t pixels_milli =
            scope_ratio((int64_t)scope_state.trigger.configured.code * 25000, 13) -
            (int64_t)scope_state.channels.position_millidivisions[channel] * 50;
        level_uv = scope_ratio(pixels_milli * scope_scale_mv(scope_state.channels.range[channel]) *
                                   scope_state.channels.probe_multiplier[channel],
                               50);
    }
    level_uv += direction * step_uv;
    if (level_uv >= -1000000000 && level_uv <= 1000000000) {
        front_panel_issue_value("TRIGGER LEVEL ", level_uv);
    }
}

void front_panel_center_trigger(void) {
    unsigned channel = scope_state.trigger.configured.source;
    if (!scope_state.frame.sequence || !scope_state.frame.valid_count ||
        scope_state.frame.words[8 + channel] != scope_state.channels.range[channel]) {
        return;
    }
    const int8_t *samples = (const int8_t *)(scope_state.frame.words + 16);
    unsigned first = scope_state.frame.valid_start;
    unsigned count = scope_state.frame.valid_count;
    unsigned alignment = channel ? scope_state.frame.channel_alignment : 0;
    if (first + count + alignment > 2000) {
        return;
    }
    int minimum = 127, maximum = -128;
    for (unsigned index = first; index < first + count; index++) {
        int sample = samples[channel * 2000 + index + alignment];
        if (sample < minimum) {
            minimum = sample;
        }
        if (sample > maximum) {
            maximum = sample;
        }
    }
    struct scope_voltage_cal calibration;
    scope_get_voltage_cal(channel, scope_state.channels.range[channel], &calibration);
    unsigned offset_dac = (scope_state.frame.words[13] >> (channel * 16)) & 65535;
    int64_t level_uv = scope_voltage_uv(&calibration, (minimum + maximum) / 2, offset_dac) *
                       scope_state.channels.probe_multiplier[channel];
    if (level_uv >= -1000000000 && level_uv <= 1000000000) {
        front_panel_issue_value("TRIGGER LEVEL ", level_uv);
    }
}

static void panel_dispatch(unsigned event, unsigned steps) {
    switch (event) {
    case PANEL_CH1_SCALE_UP:
    case PANEL_CH1_SCALE_DOWN:
    case PANEL_CH2_SCALE_UP:
    case PANEL_CH2_SCALE_DOWN:
        panel_change_range(event <= PANEL_CH2_SCALE_DOWN,
                           (event == PANEL_CH1_SCALE_UP || event == PANEL_CH2_SCALE_UP ? -1 : 1) *
                               (int)steps);
        break;
    case PANEL_CH1_POSITION_UP:
    case PANEL_CH1_POSITION_DOWN:
    case PANEL_CH2_POSITION_UP:
    case PANEL_CH2_POSITION_DOWN:
        panel_change_position(
            event >= PANEL_CH2_POSITION_UP,
            (event == PANEL_CH1_POSITION_UP || event == PANEL_CH2_POSITION_UP ? 1 : -1) *
                (int)steps);
        break;
    case PANEL_CH1_CENTER:
    case PANEL_CH2_CENTER:
        front_panel_issue_value(event == PANEL_CH2_CENTER ? "CH2 POSITION " : "CH1 POSITION ", 0);
        break;
    case PANEL_TIMEBASE_FASTER:
    case PANEL_TIMEBASE_SLOWER: {
        unsigned current = scope_state.capture.pending_rate_index
                               ? scope_state.capture.pending_rate_index
                               : scope_state.capture.rate_index;
        int next = (int)current + (event == PANEL_TIMEBASE_FASTER ? -1 : 1) * (int)steps;
        if (next < 1) {
            next = 1;
        } else if (next >= (int)SCOPE_RATE_COUNT) {
            next = SCOPE_RATE_COUNT - 1;
        }
        if (next != (int)current) {
            front_panel_issue_value("CAPTURE RATE ", next);
        }
        break;
    }
    case PANEL_HORIZONTAL_RIGHT:
    case PANEL_HORIZONTAL_LEFT: {
        int64_t step_ns =
            scope_ratio((int64_t)scope_state.display.view_points * 1000000000LL *
                            sample_rate_denominator(scope_state.capture.rate_index),
                        (uint64_t)sample_rate_numerator(scope_state.capture.rate_index) *
                            DISPLAY_GRID_COLUMNS * 10);
        if (step_ns < 1) {
            step_ns = 1;
        }
        int32_t current_offset_ns = scope_state.configuration.acquisition.horizontal_enabled
                                        ? scope_state.configuration.acquisition.horizontal_ns
                                        : 0;
        int64_t offset_ns = current_offset_ns +
                            (event == PANEL_HORIZONTAL_RIGHT ? step_ns : -step_ns) * steps;
        if (offset_ns >= -1000000000 && offset_ns <= 1000000000) {
            front_panel_issue_value("HORIZONTAL OFFSET ", offset_ns);
        }
        break;
    }
    case PANEL_HORIZONTAL_CENTER:
        front_panel_issue("HORIZONTAL STOCK");
        break;
    case PANEL_TRIGGER_UP:
    case PANEL_TRIGGER_DOWN:
        panel_change_trigger((event == PANEL_TRIGGER_UP ? 1 : -1) * (int)steps);
        break;
    case PANEL_TRIGGER_CENTER:
        front_panel_center_trigger();
        break;
    case PANEL_RUN_STOP:
        if (scope_state.record.active) {
            front_panel_issue("RECORD CANCEL");
            front_panel_issue("LIVE OFF");
        } else if (scope_state.trigger.configured.sweep == 2) {
            front_panel_issue("LIVE RUN");
        } else {
            front_panel_issue(scope_state.capture.live_enabled ? "LIVE OFF" : "LIVE RUN");
        }
        break;
    case PANEL_COPY:
        screenshot_begin();
        front_panel_menu_toggle(PANEL_MENU_COPY);
        break;
    case PANEL_FORCE:
        front_panel_issue("TRIGGER FORCE");
        break;
    case PANEL_CH1_MENU:
    case PANEL_CH2_MENU:
        selected_channel = event == PANEL_CH2_MENU;
        front_panel_menu_toggle(selected_channel ? PANEL_MENU_CH2 : PANEL_MENU_CH1);
        break;
    case PANEL_TRIGGER_MENU:
        front_panel_menu_toggle(PANEL_MENU_TRIGGER);
        break;
    case PANEL_HORIZONTAL_MENU:
        front_panel_menu_toggle(PANEL_MENU_HORIZONTAL);
        break;
    case PANEL_ACQUIRE_MENU:
        front_panel_menu_toggle(PANEL_MENU_ACQUIRE);
        break;
    case PANEL_UTILITY_MENU:
        front_panel_menu_toggle(PANEL_MENU_UTILITY);
        break;
    case PANEL_MEASURE_MENU:
        front_panel_menu_toggle(PANEL_MENU_MEASURE);
        break;
    case PANEL_CURSOR_MENU:
        front_panel_menu_toggle(PANEL_MENU_CURSOR);
        break;
    case PANEL_MATH:
        front_panel_menu_toggle(PANEL_MENU_MATH);
        break;
    case PANEL_AUTOSET:
        panel_autoset_start();
        front_panel_menu_toggle(PANEL_MENU_AUTOSET);
        break;
    case PANEL_RIGHT_1:
        front_panel_menu_action(0);
        break;
    case PANEL_RIGHT_2:
    case PANEL_RIGHT_3:
    case PANEL_RIGHT_4:
    case PANEL_RIGHT_5:
        front_panel_menu_action(event - PANEL_RIGHT_2 + 1);
        break;
    case PANEL_GENERAL_COUNTERCLOCKWISE:
    case PANEL_GENERAL_CLOCKWISE:
        front_panel_menu_rotate((event == PANEL_GENERAL_CLOCKWISE ? 1 : -1) * (int)steps);
        break;
    case PANEL_GENERAL_SELECT:
        front_panel_menu_activate();
        break;
    default:
        break;
    }
}

static void panel_enqueue(unsigned event, unsigned steps) {
    if (!steps) {
        return;
    }
    if (steps > 64) {
        dropped_events += steps - 64;
        steps = 64;
    }
    if (event <= PANEL_GENERAL_CLOCKWISE && pending_count) {
        unsigned tail = (pending_head + pending_count - 1) % 16;
        struct panel_pending_event *pending = &pending_events[tail];
        if ((pending->code - 1) / 2 == (event - 1) / 2) {
            int combined =
                (int)pending->steps + (pending->code == event ? (int)steps : -(int)steps);
            if (!combined) {
                pending_count--;
            } else {
                if (combined < 0) {
                    pending->code = event;
                    combined = -combined;
                }
                if (combined > 64) {
                    dropped_events += combined - 64;
                    combined = 64;
                }
                pending->steps = combined;
            }
            return;
        }
    }
    if (pending_count == 16) {
        dropped_events += steps;
        return;
    }
    unsigned tail = (pending_head + pending_count) % 16;
    pending_events[tail] = (struct panel_pending_event){event, steps};
    pending_count++;
}

static void panel_receive_event(void) {
    if (timer_has_elapsed(last_poll_ticks, 5)) {
        last_poll_ticks = timer_ticks();
        uint32_t ready, event, steps = 1;
        if (fpga_read_register(PANEL_EVENT_READY, &ready) || ready != 1 ||
            fpga_read_register(PANEL_EVENT_CODE, &event)) {
            return;
        }
        if (event >= PANEL_CH2_SCALE_UP && event <= PANEL_GENERAL_CLOCKWISE &&
            fpga_read_register(PANEL_ENCODER_STEPS, &steps)) {
            return;
        }
        panel_autoset_cancel();
        event_count++;
        last_event = event;
        last_steps = steps;
        if (event == PANEL_RUN_STOP || event == PANEL_FORCE || event == PANEL_RIGHT_5) {
            pending_count = 0;
            panel_dispatch(event, 1);
        } else if (event && event <= PANEL_COPY) {
            panel_enqueue(event, event <= PANEL_GENERAL_CLOCKWISE ? steps : 1);
        }
    }
}

void front_panel_poll(void) {
    if (scope_state.boot.stage != BOOT_ACQUIRING || !scope_state.fpga.spi_ready) {
        last_led_value = 0;
        return;
    }
    panel_receive_event();
    /* Stock 0x3d728: low bits select RUN/STOP; bits 2/3 mark hidden channels. */
    unsigned led_value = scope_state.capture.live_enabled || scope_state.record.active ? 2 : 1;
    led_value |= (!scope_state.channels.visible[0] << 2) | (!scope_state.channels.visible[1] << 3);
    if (last_led_value != led_value && !fpga_write_register(PANEL_LED_CONTROL, led_value)) {
        last_led_value = led_value;
    }
    if (!pending_count) {
        return;
    }
    struct panel_pending_event *pending = &pending_events[pending_head];
    unsigned menu_navigation = pending->code == PANEL_GENERAL_CLOCKWISE ||
                               pending->code == PANEL_GENERAL_COUNTERCLOCKWISE ||
                               pending->code == PANEL_GENERAL_SELECT;
    if (!menu_navigation &&
        (scope_state.analog.state == SETUP_RUNNING || scope_state.adc.state == SETUP_RUNNING ||
         scope_state.record.active || scope_state.settings.restore_state == 1)) {
        return;
    }
    if (pending->steps) {
        panel_dispatch(pending->code, pending->steps);
    }
    pending_head = (pending_head + 1) % 16;
    pending_count--;
}

unsigned front_panel_command(const uint8_t *command, unsigned length, uint8_t *reply) {
    if (!command_equals(command, length, "PANEL STATUS")) {
        return 0;
    }
    const char *names[] = {"PANEL events=",
                           " last=",
                           " steps=",
                           " queued=",
                           " dropped=",
                           " errors=",
                           " channel=",
                           " menu=",
                           " selected=",
                           " editing=",
                           " page="};
    unsigned values[] = {event_count,
                         last_event,
                         last_steps,
                         pending_count,
                         dropped_events,
                         command_errors,
                         selected_channel,
                         0,
                         0,
                         0,
                         0};
    front_panel_menu_state(values + 7);
    unsigned reply_length = 0;
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        reply_length += reply_write_text(reply + reply_length, names[i]);
        reply_length += reply_write_hex32(reply + reply_length, values[i]);
    }
    reply[reply_length++] = '\n';
    return reply_length;
}
