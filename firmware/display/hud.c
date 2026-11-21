#include "scope_internal.h"

#include "hud_font.h"
#include "panel_tools.h"

static unsigned
hud_format_scaled_value(char *text, int64_t value, const char *unit, uint64_t divisor);
unsigned hud_format_voltage(char *text, int64_t voltage_uv);
unsigned hud_format_time(char *text, int64_t time_ns);
static unsigned plot_rate_hz(void);
static unsigned plot_rate_index(void);
static void hud_draw_channel_markers(void);

unsigned hud_text(unsigned x, unsigned y, const char *text, unsigned color) {
    volatile uint16_t *framebuffer = scope_state.display.draw_framebuffer;
    color = display_foreground_color(color);
    unsigned background = display_background_color();
    while (*text && x + 6 <= 800) {
        unsigned character_code = (unsigned char)*text++;
        for (unsigned row = 0; row < 7; row++) {
            for (unsigned column = 0; column < 5; column++) {
                framebuffer[(y + row) * 800 + x + column] =
                    (character_code < 128 && (hud_font[character_code][row] & (16u >> column)))
                        ? color
                        : background;
            }
        }
        x += 6;
    }
    return x;
}

unsigned hud_number(unsigned x, unsigned y, int value, unsigned color) {
    char reverse_digits[12];
    unsigned digit_count = 0;
    if (value < 0) {
        x = hud_text(x, y, "-", color);
        value = -value;
    }
    do {
        reverse_digits[digit_count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value && digit_count < 11);
    while (digit_count) {
        char digit_text[2] = {reverse_digits[--digit_count], 0};
        x = hud_text(x, y, digit_text, color);
    }
    return x;
}

static unsigned
hud_format_scaled_value(char *text, int64_t value, const char *unit, uint64_t divisor) {
    int64_t scaled_milliths = scope_ratio(value * 1000, divisor);
    unsigned text_length = 0;
    if (scaled_milliths < 0) {
        text[text_length++] = '-';
        scaled_milliths = -scaled_milliths;
    }
    unsigned whole = (unsigned)scope_ratio(scaled_milliths, 1000),
             fractional_part = (unsigned)(scaled_milliths - (int64_t)whole * 1000);
    char reverse[12];
    unsigned digits = 0;
    do {
        reverse[digits++] = (char)('0' + whole % 10);
        whole /= 10;
    } while (whole);
    while (digits) {
        text[text_length++] = reverse[--digits];
    }
    if (fractional_part) {
        text[text_length++] = '.';
        text[text_length++] = (char)('0' + fractional_part / 100);
        if (fractional_part % 100) {
            text[text_length++] = (char)('0' + fractional_part / 10 % 10);
            if (fractional_part % 10) {
                text[text_length++] = (char)('0' + fractional_part % 10);
            }
        }
    }
    while (*unit) {
        text[text_length++] = *unit++;
    }
    text[text_length] = 0;
    return text_length;
}

unsigned hud_format_quantity(char *text, int64_t value_micro, const char *unit) {
    return hud_format_scaled_value(text, value_micro, unit, 1000000);
}

unsigned hud_format_frequency(char *text, uint64_t frequency_millihz) {
    return frequency_millihz >= 1000000000
               ? hud_format_scaled_value(text, frequency_millihz, "MHz", 1000000000)
           : frequency_millihz >= 1000000
               ? hud_format_scaled_value(text, frequency_millihz, "kHz", 1000000)
               : hud_format_scaled_value(text, frequency_millihz, "Hz", 1000);
}

unsigned hud_format_voltage(char *text, int64_t voltage_uv) {
    int64_t magnitude = voltage_uv < 0 ? -voltage_uv : voltage_uv;
    return magnitude >= 1000000000LL ? hud_format_scaled_value(text, voltage_uv, "kV", 1000000000u)
           : magnitude >= 1000000    ? hud_format_scaled_value(text, voltage_uv, "V", 1000000)
           : magnitude >= 1000       ? hud_format_scaled_value(text, voltage_uv, "mV", 1000)
                                     : hud_format_scaled_value(text, voltage_uv, "uV", 1);
}

unsigned hud_format_time(char *text, int64_t time_ns) {
    int64_t magnitude = time_ns < 0 ? -time_ns : time_ns;
    return magnitude >= 1000000000LL ? hud_format_scaled_value(text, time_ns, "s", 1000000000u)
           : magnitude >= 1000000    ? hud_format_scaled_value(text, time_ns, "ms", 1000000)
           : magnitude >= 1000       ? hud_format_scaled_value(text, time_ns, "us", 1000)
                                     : hud_format_scaled_value(text, time_ns, "ns", 1);
}

static unsigned plot_rate_hz(void) {
    return sample_rate_numerator(plot_rate_index());
}
static unsigned plot_rate_index(void) {
    return scope_state.frame.sequence ? scope_state.frame.words[14]
                                      : scope_state.capture.rate_index;
}

void hud_draw_usb_status(void) {
    static const uint16_t usb_icon_rows[20] = {
        0x0100, 0x0380, 0x07c0, 0x0100, 0x0100, 0x0100, 0x011c, 0x191c, 0x191c, 0x1908,
        0x0908, 0x0508, 0x0308, 0x0110, 0x0120, 0x0140, 0x0380, 0x07c0, 0x07c0, 0x0380,
    };
    volatile uint16_t *framebuffer = scope_state.display.draw_framebuffer;
    unsigned activity = usb_activity_flags();
    display_fill_rectangle(742, 0, 56, 26, display_background_color());
    if (!(activity & USB_CLIENT_CONNECTED)) {
        return;
    }
    hud_text(744, 4, "TX", 0x7bef);
    hud_text(744, 15, "RX", 0x7bef);
    for (unsigned row = 0; row < 4; row++) {
        for (unsigned column = 0; column < 4; column++) {
            framebuffer[(row + 5) * 800 + 763 + column] =
                activity & USB_TRANSMIT_ACTIVE ? display_foreground_color(0x07e0) : 0x2104;
            framebuffer[(row + 16) * 800 + 763 + column] =
                activity & USB_RECEIVE_ACTIVE ? display_foreground_color(0xffe0) : 0x2104;
        }
    }
    for (unsigned row = 0; row < 20; row++) {
        for (unsigned column = 0; column < 16; column++) {
            framebuffer[(row + 3) * 800 + 778 + column] = usb_icon_rows[row] & (1u << (15 - column))
                                                              ? display_foreground_color(0xffff)
                                                              : display_background_color();
        }
    }
}

void hud_draw_status(void) {
    char label[28];
    unsigned x = 8;
    display_fill_rectangle(0, 0, 800, 26, display_background_color());
    for (unsigned channel_index = 0; channel_index < 2; channel_index++) {
        unsigned color = channel_index ? 0x07ff : 0xffe0;
        struct scope_voltage_cal voltage_calibration;
        scope_get_voltage_cal(
            channel_index, scope_state.channels.range[channel_index], &voltage_calibration);
        x = hud_text(x, 4, channel_index ? "  CH2:" : "CH1:", color);
        hud_format_voltage(label,
                           (int64_t)scope_scale_mv(scope_state.channels.range[channel_index]) *
                               1000 * scope_state.channels.probe_multiplier[channel_index]);
        x = hud_text(x, 4, label, color);
        x = hud_text(x,
                     4,
                     !scope_voltage_baseline()        ? "/DIV STOCK"
                     : voltage_calibration.valid == 1 ? "/DIV REF"
                     : voltage_calibration.valid == 2 ? "/DIV ZERO"
                                                      : "/DIV NOM",
                     color);
    }
    x = hud_text(x, 4, "  M:", 0xffff);
    hud_format_time(label,
                    scope_ratio((int64_t)scope_state.display.view_points * 1000000000LL *
                                    sample_rate_denominator(plot_rate_index()),
                                (uint64_t)plot_rate_hz() * DISPLAY_GRID_COLUMNS));
    x = hud_text(x, 4, label, 0xffff);
    x = hud_text(x, 4, "/DIV  H:", 0xffff);
    int32_t horizontal_offset_ns = scope_state.configuration.acquisition.horizontal_enabled &&
                                           !scope_state.capture.capture_roll
                                       ? scope_state.configuration.acquisition.horizontal_ns
                                       : 0;
    if (horizontal_offset_ns > 0) {
        x = hud_text(x, 4, "+", 0xffff);
    }
    hud_format_time(label, horizontal_offset_ns);
    hud_text(x, 4, label, 0xffff);
    x = hud_text(8,
                 16,
                 scope_state.capture.live_enabled ? "RUN " : "STOP ",
                 scope_state.capture.live_enabled ? 0x07e0 : 0xf800);
    x = hud_text(
        x, 16, scope_state.configuration.features.trigger_family ? "ALT: " : "TRIG: ", 0xffff);
    x = hud_text(x, 16, scope_state.trigger.configured.source ? "CH2 " : "CH1 ", 0xffff);
    x = hud_text(x, 16, scope_state.trigger.configured.slope ? "FALL " : "RISE ", 0xffff);
    static const char *modes[] = {
        "EDGE ", "VIDEO ", "PULSE ", "SLOPE ", "RUNT ", "SPIKE ", "TIMEOUT ", "NTHEDGE "};
    x = hud_text(x,
                 16,
                 scope_state.trigger.advanced.mode == 2 && !scope_state.pulse.native &&
                         !scope_state.configuration.features.trigger_family
                     ? "PULSE SW "
                     : modes[scope_state.trigger.advanced.mode],
                 0xffff);
    if (scope_state.trigger.configured.use_code) {
        x = hud_number(x, 16, scope_state.trigger.configured.code, 0xffff);
        x = hud_text(x, 16, " CODE ", 0xffff);
    } else {
        hud_format_voltage(label, scope_state.trigger.configured.level_uv);
        x = hud_text(x, 16, label, 0xffff);
        x = hud_text(x, 16, " NOM ", 0xffff);
    }
    x = hud_text(x,
                 16,
                 scope_state.capture.capture_roll
                     ? (scope_state.capture.roll_filling ? "ROLL WAIT " : "ROLL ")
                 : !scope_state.trigger.enabled              ? "FREE "
                 : scope_state.trigger.configured.sweep == 0 ? "AUTO "
                 : scope_state.trigger.configured.sweep == 1 ? "NORMAL "
                                                             : "SINGLE ",
                 0xffff);
    hud_text(x,
             16,
             scope_state.configuration.features.acquire_mode == 1   ? "PEAK"
             : scope_state.configuration.features.acquire_mode == 2 ? "AVG"
                                                                    : "SAMPLE",
             0xffff);
}

static __attribute__((section(".panel_text"), noinline)) void hud_draw_labeled_arrow(
    unsigned tip_x, int center_y, unsigned width, unsigned color, const char *label) {
    volatile uint16_t *framebuffer = scope_state.display.draw_framebuffer;
    unsigned fill = display_foreground_color(color);
    unsigned ink = display_background_color();
    for (int offset_y = -6; offset_y <= 6; offset_y++) {
        unsigned inset = offset_y < 0 ? -offset_y : offset_y;
        for (unsigned offset_x = inset; offset_x < width; offset_x++) {
            framebuffer[(center_y + offset_y) * 800 + tip_x + offset_x] = fill;
        }
    }
    unsigned text_x = tip_x + 6;
    while (*label) {
        unsigned character_code = (unsigned char)*label++;
        for (unsigned row = 0; row < 7; row++) {
            for (unsigned column = 0; column < 5; column++) {
                if (hud_font[character_code][row] & (16u >> column)) {
                    framebuffer[(center_y - 3 + row) * 800 + text_x + column] = ink;
                }
            }
        }
        text_x += 6;
    }
}

static __attribute__((section(".panel_text"), noinline)) void hud_draw_channel_markers(void) {
    unsigned background = display_background_color();
    display_fill_rectangle(0, 27, DISPLAY_PLOT_LEFT, 453, background);
    display_fill_rectangle(DISPLAY_PLOT_LEFT + DISPLAY_PLOT_WIDTH + 1,
                           27,
                           800 - DISPLAY_PLOT_LEFT - DISPLAY_PLOT_WIDTH - 1,
                           453,
                           background);
    display_fill_rectangle(DISPLAY_PLOT_LEFT,
                           DISPLAY_PLOT_TOP + DISPLAY_PLOT_HEIGHT + 1,
                           DISPLAY_PLOT_WIDTH + 1,
                           479 - DISPLAY_PLOT_TOP - DISPLAY_PLOT_HEIGHT,
                           background);
    if (panel_math_mode() == 3 || panel_math_mode() == 6) {
        return;
    }
    unsigned marker_left = DISPLAY_PLOT_LEFT + DISPLAY_PLOT_WIDTH + 1;
    for (unsigned channel = 0; channel < 2; channel++) {
        int zero_y = 240 - scope_state.channels.position_millidivisions[channel] * 50 / 1000;
        if (scope_state.channels.visible[channel] && zero_y >= 40 && zero_y <= 440) {
            hud_draw_labeled_arrow(marker_left + channel * 12,
                                   zero_y,
                                   12,
                                   channel ? 0x07ff : 0xffe0,
                                   channel ? "2" : "1");
        }
    }
    if (scope_state.trigger.enabled) {
        unsigned channel = scope_state.trigger.configured.source;
        int zero_y = 240 - scope_state.channels.position_millidivisions[channel] * 50 / 1000;
        int pixels =
            scope_state.trigger.configured.use_code
                ? (int)scope_ratio((int64_t)scope_state.trigger.configured.code * 25, 13) -
                      scope_state.channels.position_millidivisions[channel] * 50 / 1000
                : (int)scope_ratio((int64_t)scope_state.trigger.configured.level_uv * 50,
                                   (uint64_t)scope_scale_mv(scope_state.channels.range[channel]) *
                                       1000 * scope_state.channels.probe_multiplier[channel]);
        int trigger_y = zero_y - pixels * (scope_state.channels.inverse[channel] ? -1 : 1);
        if (trigger_y < 40) {
            trigger_y = 40;
        } else if (trigger_y > 440) {
            trigger_y = 440;
        }
        hud_draw_labeled_arrow(marker_left + 24, trigger_y, 19, 0xffff, channel ? "T2" : "T1");
    }
}

void display_annotations(void) {
    hud_draw_channel_markers();
}

void hud_draw_measurements(void) {
    display_fill_rectangle(
        DISPLAY_PLOT_LEFT, 458, DISPLAY_PLOT_WIDTH, 7, display_background_color());
    display_fill_rectangle(
        DISPLAY_PLOT_LEFT, 470, DISPLAY_PLOT_WIDTH, 7, display_background_color());
    for (unsigned channel = 0; channel < 2; channel++) {
        unsigned color = channel ? 0x07ff : 0xffe0;
        unsigned x = hud_text(channel ? 425 : 25, 458, channel ? "CH2 VPP: " : "CH1 VPP: ", color);
        unsigned maximum_x =
            hud_text(channel ? 425 : 25, 470, channel ? "CH2 MAX: " : "CH1 MAX: ", color);
        unsigned range = scope_state.frame.words[8 + channel];
        if (!frame_channel_matches_live_range(channel) || !scope_state.frame.valid_count ||
            range >= 14 ||
            scope_state.frame.calibration_ids[channel] !=
                scope_adc_calibration_id(channel, range)) {
            hud_text(x, 458, "--", color);
            hud_text(maximum_x, 470, "--", color);
            continue;
        }
        const int8_t *samples = (const int8_t *)(scope_state.frame.words + 16) + channel * 2000 +
                                (channel ? scope_state.frame.channel_alignment : 0);
        int low_code = 127, high_code = -128;
        unsigned end = scope_state.frame.valid_start + scope_state.frame.valid_count;
        if (end > 1998) {
            end = 1998;
        }
        for (unsigned index = scope_state.frame.valid_start; index < end; index++) {
            int code = samples[index];
            if (code < low_code) {
                low_code = code;
            }
            if (code > high_code) {
                high_code = code;
            }
            if (!(index & 255)) {
                usb_service();
            }
        }
        if (low_code > high_code) {
            hud_text(x, 458, "--", color);
            hud_text(maximum_x, 470, "--", color);
            continue;
        }
        struct scope_voltage_cal calibration;
        scope_get_voltage_cal(channel, range, &calibration);
        int64_t peak_to_peak_uv = scope_ratio((int64_t)(high_code - low_code) * calibration.gain *
                                                  scope_state.channels.probe_multiplier[channel],
                                              256);
        char voltage[24];
        hud_format_voltage(voltage, peak_to_peak_uv);
        x = hud_text(x, 458, voltage, color);
        if (low_code <= -124 || high_code >= 124) {
            hud_text(x, 458, " CLIP", 0xf800);
        }
        unsigned offset_dac = (scope_state.frame.words[13] >> (channel * 16)) & 65535u;
        int64_t maximum_uv = scope_voltage_uv(&calibration, high_code, offset_dac) *
                             scope_state.channels.probe_multiplier[channel];
        hud_format_voltage(voltage, maximum_uv);
        hud_text(maximum_x, 470, voltage, color);
    }
}
