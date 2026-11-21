#include "panel_tools.h"
#include "front_panel.h"
#include "scope_internal.h"
#include "spectrum.h"

static unsigned cursor_mode, cursor_source, cursor_selected;
static int cursor_x[2] = {187, 562}, cursor_y[2] = {140, 340};
static unsigned old_cursor_mode[2];
static int old_cursor_positions[2][2];
static unsigned math_mode, math_range = 9, math_probe = 1, math_reversed;
static int math_position_millidivisions;
static int16_t math_trace_y[2][DISPLAY_PLOT_WIDTH];
static unsigned math_trace_valid[2], math_trace_xy[2];
static int16_t math_trace_x[2][DISPLAY_PLOT_WIDTH];

unsigned panel_cursor_mode(void) {
    return cursor_mode;
}
unsigned panel_cursor_source(void) {
    return cursor_source;
}
unsigned panel_cursor_selected(void) {
    return cursor_selected;
}
unsigned panel_math_mode(void) {
    return math_mode;
}
void panel_math_select_fft(unsigned range) {
    math_mode = 3;
    math_range = range;
    math_probe = scope_state.channels.probe_multiplier[panel_fft_source()];
}
unsigned panel_math_range(void) {
    return math_range;
}
unsigned panel_math_reversed(void) {
    return math_reversed;
}
int panel_math_position(void) {
    return math_position_millidivisions;
}
uint64_t panel_math_scale_uv(void) {
    return (uint64_t)scope_scale_mv(math_range) * 1000 * math_probe;
}

void panel_math_format_value(char *text, int64_t value_micro) {
    if (math_mode == 4 || math_mode == 5) {
        hud_format_quantity(text, value_micro, math_mode == 4 ? "V^2" : "");
    } else {
        hud_format_voltage(text, value_micro);
    }
}

void panel_cursor_action(unsigned action) {
    if (action == 0) {
        cursor_mode = (cursor_mode + 1) % 3;
    } else if (action == 1) {
        cursor_source ^= 1;
    } else if (action == 2 || action == 3) {
        cursor_selected = action - 2;
        if (!cursor_mode) {
            cursor_mode = 1;
        }
    } else {
        cursor_x[0] = 187;
        cursor_x[1] = 562;
        cursor_y[0] = 140;
        cursor_y[1] = 340;
    }
    front_panel_menu_refresh();
}

void panel_cursor_move(int steps) {
    if (cursor_mode == 1) {
        int position = cursor_x[cursor_selected] + steps;
        if (position < 0) {
            position = 0;
        }
        if (position >= DISPLAY_PLOT_WIDTH) {
            position = DISPLAY_PLOT_WIDTH - 1;
        }
        cursor_x[cursor_selected] = position;
    } else if (cursor_mode == 2) {
        int position = cursor_y[cursor_selected] - steps;
        if (position < DISPLAY_PLOT_TOP) {
            position = DISPLAY_PLOT_TOP;
        }
        if (position >= DISPLAY_PLOT_TOP + DISPLAY_PLOT_HEIGHT) {
            position = DISPLAY_PLOT_TOP + DISPLAY_PLOT_HEIGHT - 1;
        }
        cursor_y[cursor_selected] = position;
    }
    scope_state.display.display_redraw_requested = 1;
}

void panel_math_adjust(unsigned action, int steps) {
    if (math_mode == 3 && action == 4) {
        if (panel_fft_logarithmic()) {
            panel_fft_adjust(steps);
        } else {
            panel_math_adjust(1, -steps);
        }
        return;
    }
    if (action == 1) {
        int range = (int)math_range - steps;
        if (range < 0) {
            range = 0;
        }
        if (range > 13) {
            range = 13;
        }
        math_range = range;
    } else if (action == 2) {
        int position = math_position_millidivisions + steps * 100;
        if (position < -4000) {
            position = -4000;
        }
        if (position > 4000) {
            position = 4000;
        }
        math_position_millidivisions = position;
    }
    front_panel_menu_refresh();
}

void panel_math_action(unsigned action) {
    if (action == 0) {
        math_mode = (math_mode + 1) % 7;
        if (math_mode == 3) {
            math_probe = scope_state.channels.probe_multiplier[panel_fft_source()];
        }
        if (math_mode == 4 || math_mode == 5) {
            math_probe = 1;
            math_range = 9;
        }
        if (math_mode == 6) {
            math_probe = scope_state.channels.probe_multiplier[1];
            math_range = scope_state.channels.range[1];
        }
        if (math_mode == 1) {
            math_range = scope_state.channels.range[0];
            math_probe = scope_state.channels.probe_multiplier[0];
        }
    } else if (math_mode == 3) {
        panel_fft_action(action);
        math_probe = scope_state.channels.probe_multiplier[panel_fft_source()];
    } else if (action == 1) {
        panel_math_adjust(1, -1);
    } else if (action == 2) {
        math_position_millidivisions = 0;
    } else if (action == 3) {
        if (math_mode == 2) {
            math_reversed ^= 1;
        }
    } else {
        math_range = scope_state.channels.range[0];
        math_probe = scope_state.channels.probe_multiplier[0];
        math_position_millidivisions = 0;
    }
    front_panel_menu_refresh();
}

static void draw_cursor(unsigned mode, int position, unsigned color, unsigned erase) {
    volatile uint16_t *framebuffer = scope_state.display.draw_framebuffer;
    unsigned length = mode == 1 ? DISPLAY_PLOT_HEIGHT : DISPLAY_PLOT_WIDTH;
    for (unsigned index = 0; index < length; index++) {
        if (index % 6 >= 3) {
            continue;
        }
        unsigned x = mode == 1 ? DISPLAY_PLOT_LEFT + position : DISPLAY_PLOT_LEFT + index;
        unsigned y = mode == 1 ? DISPLAY_PLOT_TOP + index : position;
        framebuffer[y * 800 + x] =
            erase ? display_grid_color_at(x, y) : display_foreground_color(color);
    }
}

void panel_tools_erase(void) {
    unsigned buffer = scope_state.display.draw_buffer_index;
    if (old_cursor_mode[buffer]) {
        for (unsigned cursor = 0; cursor < 2; cursor++) {
            draw_cursor(old_cursor_mode[buffer], old_cursor_positions[buffer][cursor], 0, 1);
        }
        old_cursor_mode[buffer] = 0;
    }
    if (math_trace_valid[buffer]) {
        for (unsigned x = 0; x < DISPLAY_PLOT_WIDTH; x++) {
            if (math_trace_y[buffer][x] != -32768) {
                int previous_y =
                    x && math_trace_y[buffer][x - 1] != -32768 && !math_trace_xy[buffer]
                        ? math_trace_y[buffer][x - 1]
                        : math_trace_y[buffer][x];
                display_draw_trace_segment(math_trace_xy[buffer] ? math_trace_x[buffer][x]
                                                                 : DISPLAY_PLOT_LEFT + x,
                                           math_trace_y[buffer][x],
                                           previous_y,
                                           0,
                                           1);
            }
            if (!(x & 63)) {
                usb_service();
            }
        }
        math_trace_valid[buffer] = 0;
    }
}

static __attribute__((section(".panel_text"), noinline)) void draw_math_trace(void) {
    math_trace_xy[scope_state.display.draw_buffer_index] = math_mode == 6;
    if (math_mode == 3) {
        unsigned buffer = scope_state.display.draw_buffer_index;
        if (!panel_fft_prepare()) {
            return;
        }
        for (unsigned x = 0; x < DISPLAY_PLOT_WIDTH; x++) {
            int y = panel_fft_y(x, panel_math_scale_uv());
            math_trace_y[buffer][x] = y;
            display_draw_trace_segment(
                DISPLAY_PLOT_LEFT + x, y, x ? math_trace_y[buffer][x - 1] : y, 0xf81f, 0);
            if (!(x & 31)) {
                usb_service();
            }
        }
        math_trace_valid[buffer] = 1;
        return;
    }
    if (!math_mode || !scope_state.frame.valid_count || !frame_channel_matches_live_range(0) ||
        !frame_channel_matches_live_range(1)) {
        return;
    }
    struct scope_voltage_cal calibration[2];
    int64_t voltage_uv[2][256];
    for (unsigned channel = 0; channel < 2; channel++) {
        unsigned range = scope_state.frame.words[8 + channel];
        if (range >= 14 || scope_state.frame.calibration_ids[channel] !=
                               scope_adc_calibration_id(channel, range)) {
            return;
        }
        scope_get_voltage_cal(channel, range, &calibration[channel]);
        unsigned offset_dac = (scope_state.frame.words[13] >> (channel * 16)) & 65535;
        for (unsigned code = 0; code < 256; code++) {
            voltage_uv[channel][code] =
                scope_voltage_uv(&calibration[channel], (int8_t)code, offset_dac) *
                scope_state.channels.probe_multiplier[channel];
            if (!(code & 63)) {
                usb_service();
            }
        }
    }
    unsigned buffer = scope_state.display.draw_buffer_index;
    const uint8_t *samples = (const uint8_t *)(scope_state.frame.words + 16);
    uint64_t scale_uv = (uint64_t)scope_scale_mv(math_range) * 1000 * math_probe;
    unsigned first = scope_state.frame.is_roll ? 1998 - scope_state.display.view_points
                                               : scope_state.display.view_start;
    for (unsigned x = 0; x < DISPLAY_PLOT_WIDTH; x++) {
        if (!(x & 31)) {
            usb_service();
        }
        unsigned index = first + x * scope_state.display.view_points / DISPLAY_PLOT_WIDTH;
        math_trace_y[buffer][x] = -32768;
        if (index < scope_state.frame.valid_start ||
            index >= scope_state.frame.valid_start + scope_state.frame.valid_count) {
            continue;
        }
        unsigned ch1_code = samples[index];
        unsigned ch2_code = samples[2000 + index + scope_state.frame.channel_alignment];
        int64_t value_uv = math_mode == 1 ? voltage_uv[0][ch1_code] + voltage_uv[1][ch2_code]
                                          : voltage_uv[0][ch1_code] - voltage_uv[1][ch2_code];
        if (math_mode == 4) {
            int64_t left_uv = voltage_uv[0][ch1_code], right_uv = voltage_uv[1][ch2_code];
            value_uv = left_uv > 1000000000LL || left_uv < -1000000000LL ||
                               right_uv > 1000000000LL || right_uv < -1000000000LL
                           ? scope_ratio(left_uv, 1000) * scope_ratio(right_uv, 1000)
                           : scope_ratio(left_uv * right_uv, 1000000);
        } else if (math_mode == 5) {
            if (!voltage_uv[1][ch2_code]) {
                continue;
            }
            int64_t denominator = voltage_uv[1][ch2_code];
            value_uv = scope_ratio(voltage_uv[0][ch1_code] * (denominator < 0 ? -1000000 : 1000000),
                                   denominator < 0 ? -denominator : denominator);
        } else if (math_mode == 6) {
            uint64_t x_scale_uv = (uint64_t)scope_scale_mv(scope_state.channels.range[0]) * 1000 *
                                  scope_state.channels.probe_multiplier[0];
            int64_t plot_x = DISPLAY_PLOT_LEFT + DISPLAY_PLOT_WIDTH / 2 +
                             scope_ratio(voltage_uv[0][ch1_code] * 50, x_scale_uv);
            int64_t plot_y = DISPLAY_PLOT_TOP + DISPLAY_PLOT_HEIGHT / 2 -
                             scope_ratio(voltage_uv[1][ch2_code] * 50, scale_uv) -
                             math_position_millidivisions * 50 / 1000;
            if (plot_x < DISPLAY_PLOT_LEFT || plot_x >= DISPLAY_PLOT_LEFT + DISPLAY_PLOT_WIDTH ||
                plot_y < DISPLAY_PLOT_TOP || plot_y > DISPLAY_PLOT_TOP + DISPLAY_PLOT_HEIGHT) {
                continue;
            }
            math_trace_x[buffer][x] = plot_x;
            math_trace_y[buffer][x] = plot_y;
            display_draw_trace_segment(plot_x, plot_y, plot_y, 0xf81f, 0);
            if (!(x & 31)) {
                usb_service();
            }
            continue;
        }
        if (math_mode == 2 && math_reversed) {
            value_uv = -value_uv;
        }
        if (value_uv > 100000000000000000LL) {
            value_uv = 100000000000000000LL;
        }
        if (value_uv < -100000000000000000LL) {
            value_uv = -100000000000000000LL;
        }
        int64_t y = DISPLAY_PLOT_TOP + DISPLAY_PLOT_HEIGHT / 2 -
                    scope_ratio(value_uv * 50, scale_uv) - math_position_millidivisions * 50 / 1000;
        if (y < -30000) {
            y = -30000;
        }
        if (y > 30000) {
            y = 30000;
        }
        math_trace_y[buffer][x] = y;
        int previous_y =
            x && math_trace_y[buffer][x - 1] != -32768 ? math_trace_y[buffer][x - 1] : y;
        display_draw_trace_segment(DISPLAY_PLOT_LEFT + x, y, previous_y, 0xf81f, 0);
        if (!(x & 31)) {
            usb_service();
        }
    }
    math_trace_valid[buffer] = 1;
}

void panel_tools_draw(void) {
    draw_math_trace();
    if (!cursor_mode || math_mode == 3 || math_mode == 6) {
        return;
    }
    unsigned buffer = scope_state.display.draw_buffer_index;
    for (unsigned cursor = 0; cursor < 2; cursor++) {
        int position = cursor_mode == 1 ? cursor_x[cursor] : cursor_y[cursor];
        draw_cursor(cursor_mode, position, cursor == cursor_selected ? 0x07e0 : 0xffff, 0);
        old_cursor_positions[buffer][cursor] = position;
    }
    old_cursor_mode[buffer] = cursor_mode;
}

void panel_tools_readouts(void) {
    if (math_mode == 3) {
        panel_fft_readouts();
        return;
    }
    char value[28];
    if (math_mode == 6) {
        unsigned x = hud_text(160, 28, "XY X:CH1 ", 0xf81f);
        hud_format_voltage(value,
                           (uint64_t)scope_scale_mv(scope_state.channels.range[0]) * 1000 *
                               scope_state.channels.probe_multiplier[0]);
        x = hud_text(x, 28, value, 0xf81f);
        x = hud_text(x, 28, "/DIV Y:CH2 ", 0xf81f);
        panel_math_format_value(value, panel_math_scale_uv());
        x = hud_text(x, 28, value, 0xf81f);
        hud_text(x, 28, "/DIV", 0xf81f);
        return;
    }
    if (cursor_mode && math_mode != 6) {
        unsigned x = hud_text(160, 28, cursor_selected ? "B  " : "A  ", 0x07e0);
        if (cursor_mode == 1) {
            int64_t delta_ns = scope_ratio(
                (int64_t)(cursor_x[1] - cursor_x[0]) * scope_state.display.view_points *
                    1000000000LL * sample_rate_denominator(scope_state.frame.words[14]),
                (uint64_t)sample_rate_numerator(scope_state.frame.words[14]) * DISPLAY_PLOT_WIDTH);
            x = hud_text(x, 28, "DT: ", 0xffff);
            hud_format_time(value, delta_ns);
        } else {
            uint64_t scale_uv =
                (uint64_t)scope_scale_mv(scope_state.channels.range[cursor_source]) * 1000 *
                scope_state.channels.probe_multiplier[cursor_source];
            int64_t delta_uv = scope_ratio((int64_t)(cursor_y[0] - cursor_y[1]) * scale_uv, 50);
            x = hud_text(x, 28, cursor_source ? "CH2 DV: " : "CH1 DV: ", 0xffff);
            hud_format_voltage(value, delta_uv);
        }
        hud_text(x, 28, value, 0xffff);
    }
    if (math_mode) {
        static const char *const labels[] = {
            "", "ADD ", "SUBTRACT ", "FFT ", "MULTIPLY ", "DIVIDE ", "XY Y:CH2 "};
        unsigned x = hud_text(465, 28, labels[math_mode], 0xf81f);
        panel_math_format_value(value, (int64_t)scope_scale_mv(math_range) * 1000 * math_probe);
        x = hud_text(x, 28, value, 0xf81f);
        hud_text(x, 28, "/DIV", 0xf81f);
    }
}

static int
panel_parse_values(const uint8_t *text, unsigned length, int32_t *values, unsigned count) {
    unsigned start = 0;
    for (unsigned index = 0; index < count; index++) {
        unsigned end = start;
        while (end < length && text[end] != ' ') {
            end++;
        }
        if (!command_parse_signed(text + start, end - start, &values[index]) ||
            (index + 1 < count ? end == length : end != length)) {
            return 0;
        }
        start = end + 1;
    }
    return 1;
}

__attribute__((section(".panel_text"), noinline)) unsigned
panel_tools_command(const uint8_t *command, unsigned length, uint8_t *reply) {
    unsigned fft_reply = panel_fft_command(command, length, reply);
    if (fft_reply) {
        return fft_reply;
    }
    if (length > 14 && command_equals(command, 14, "PANEL CURSORS ")) {
        int32_t values[7];
        if (!panel_parse_values(command + 14, length - 14, values, 7) || values[0] < 0 ||
            values[0] > 2 || values[1] < 1 || values[1] > 2 || values[2] < 0 || values[2] > 1 ||
            values[3] < 0 || values[3] >= DISPLAY_PLOT_WIDTH || values[4] < 0 ||
            values[4] >= DISPLAY_PLOT_WIDTH || values[5] < DISPLAY_PLOT_TOP ||
            values[5] >= DISPLAY_PLOT_TOP + DISPLAY_PLOT_HEIGHT || values[6] < DISPLAY_PLOT_TOP ||
            values[6] >= DISPLAY_PLOT_TOP + DISPLAY_PLOT_HEIGHT) {
            return reply_write_text(reply, "ERR invalid LCD cursors\n");
        }
        cursor_mode = values[0];
        cursor_source = values[1] - 1;
        cursor_selected = values[2];
        cursor_x[0] = values[3];
        cursor_x[1] = values[4];
        cursor_y[0] = values[5];
        cursor_y[1] = values[6];
        front_panel_menu_refresh();
        return reply_write_text(reply, "OK LCD cursors selected\n");
    }
    if (length > 11 && command_equals(command, 11, "PANEL MATH ")) {
        int32_t values[5];
        if (!panel_parse_values(command + 11, length - 11, values, 5) || values[0] < 0 ||
            values[0] > 6 || values[1] < 0 || values[1] > 13 ||
            (values[2] != 1 && values[2] != 10 && values[2] != 100 && values[2] != 1000) ||
            values[3] < -4000 || values[3] > 4000 || values[4] < 0 || values[4] > 1) {
            return reply_write_text(reply, "ERR invalid LCD math\n");
        }
        math_mode = values[0];
        math_range = values[1];
        math_probe = values[2];
        math_position_millidivisions = values[3];
        math_reversed = values[4];
        front_panel_menu_refresh();
        return reply_write_text(reply, "OK LCD math selected\n");
    }
    if (!command_equals(command, length, "PANEL TOOLS")) {
        return 0;
    }
    unsigned values[] = {cursor_mode,
                         cursor_source,
                         cursor_selected,
                         math_mode,
                         math_range,
                         cursor_x[0],
                         cursor_x[1],
                         cursor_y[0],
                         cursor_y[1],
                         math_probe,
                         (unsigned)math_position_millidivisions,
                         math_reversed,
                         panel_fft_source(),
                         panel_fft_window(),
                         panel_fft_logarithmic(),
                         panel_fft_db_per_division(),
                         (unsigned)panel_fft_reference_dbv(),
                         127};
    const char *names[] = {"TOOLS cursors=",
                           " source=",
                           " cursor=",
                           " math=",
                           " math_range=",
                           " cursor_xa=",
                           " cursor_xb=",
                           " cursor_ya=",
                           " cursor_yb=",
                           " math_probe=",
                           " math_position=",
                           " math_reversed=",
                           " fft_source=",
                           " fft_window=",
                           " fft_logarithmic=",
                           " fft_db_div=",
                           " fft_reference_dbv=",
                           " math_modes="};
    unsigned reply_length = 0;
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        reply_length += reply_write_text(reply + reply_length, names[i]);
        reply_length += reply_write_hex32(reply + reply_length, values[i]);
    }
    reply[reply_length++] = '\n';
    return reply_length;
}
