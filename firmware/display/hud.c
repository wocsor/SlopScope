#include "scope_internal.h"

#include "hud_font.h"

static unsigned
hud_format_scaled_value(char *text, int64_t value, const char *unit, uint64_t divisor);
static unsigned hud_format_voltage(char *text, int64_t voltage_uv);
static unsigned hud_format_time(char *text, int64_t time_ns);
static unsigned plot_rate_hz(void);
static void hud_draw_status(void);
static void hud_draw_channel_markers(void);

unsigned hud_text(unsigned x, unsigned y, const char *text, unsigned color) {
    volatile uint16_t *framebuffer = scope_state.display.draw_framebuffer;
    while (*text && x + 6 <= 800) {
        unsigned character_code = (unsigned char)*text++;
        for (unsigned row = 0; row < 7; row++) {
            for (unsigned column = 0; column < 5; column++) {
                framebuffer[(y + row) * 800 + x + column] =
                    (character_code < 128 && (hud_font[character_code][row] & (16u >> column)))
                        ? color
                        : 0;
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

static unsigned hud_format_voltage(char *text, int64_t voltage_uv) {
    int64_t magnitude = voltage_uv < 0 ? -voltage_uv : voltage_uv;
    return magnitude >= 1000000000LL ? hud_format_scaled_value(text, voltage_uv, "kV", 1000000000u)
           : magnitude >= 1000000    ? hud_format_scaled_value(text, voltage_uv, "V", 1000000)
           : magnitude >= 1000       ? hud_format_scaled_value(text, voltage_uv, "mV", 1000)
                                     : hud_format_scaled_value(text, voltage_uv, "uV", 1);
}

static unsigned hud_format_time(char *text, int64_t time_ns) {
    int64_t magnitude = time_ns < 0 ? -time_ns : time_ns;
    return magnitude >= 1000000000LL ? hud_format_scaled_value(text, time_ns, "s", 1000000000u)
           : magnitude >= 1000000    ? hud_format_scaled_value(text, time_ns, "ms", 1000000)
           : magnitude >= 1000       ? hud_format_scaled_value(text, time_ns, "us", 1000)
                                     : hud_format_scaled_value(text, time_ns, "ns", 1);
}

static unsigned plot_rate_hz(void) {
    return scope_state.frame.words[0] == 0x31435348 && scope_state.frame.words[5]
               ? scope_state.frame.words[5]
               : sample_rates[scope_state.capture.rate_index][2];
}

void hud_draw_usb_status(void) {
    static const uint16_t usb_icon_rows[20] = {
        0x0100, 0x0380, 0x07c0, 0x0100, 0x0100, 0x0100, 0x011c, 0x191c, 0x191c, 0x1908,
        0x0908, 0x0508, 0x0308, 0x0110, 0x0120, 0x0140, 0x0380, 0x07c0, 0x07c0, 0x0380,
    };
    volatile uint16_t *framebuffer = scope_state.display.draw_framebuffer;
    unsigned activity = usb_activity_flags();
    for (unsigned y = 0; y < 26; y++) {
        for (unsigned x = 742; x < 798; x++) {
            framebuffer[y * 800 + x] = 0;
        }
    }
    if (!(activity & USB_CLIENT_CONNECTED)) {
        return;
    }
    hud_text(744, 4, "TX", 0x7bef);
    hud_text(744, 15, "RX", 0x7bef);
    for (unsigned row = 0; row < 4; row++) {
        for (unsigned column = 0; column < 4; column++) {
            framebuffer[(row + 5) * 800 + 763 + column] =
                activity & USB_TRANSMIT_ACTIVE ? 0x07e0 : 0x2104;
            framebuffer[(row + 16) * 800 + 763 + column] =
                activity & USB_RECEIVE_ACTIVE ? 0xffe0 : 0x2104;
        }
    }
    for (unsigned row = 0; row < 20; row++) {
        for (unsigned column = 0; column < 16; column++) {
            framebuffer[(row + 3) * 800 + 778 + column] =
                usb_icon_rows[row] & (1u << (15 - column)) ? 0xffff : 0;
        }
    }
}

static void hud_draw_status(void) {
    volatile uint16_t *framebuffer = scope_state.display.draw_framebuffer;
    char label[28];
    unsigned x = 8;
    for (unsigned y = 0; y < 26; y++) {
        for (unsigned i = 0; i < 800; i++) {
            framebuffer[y * 800 + i] = 0;
        }
    }
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
                                    sample_rate_denominator(scope_state.frame.words[14]),
                                (uint64_t)plot_rate_hz() * DISPLAY_GRID_COLUMNS));
    x = hud_text(x, 4, label, 0xffff);
    hud_text(x, 4, "/DIV", 0xffff);
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
    hud_draw_usb_status();
}

static void hud_draw_channel_markers(void) {
    volatile uint16_t *framebuffer = scope_state.display.draw_framebuffer;
    /* Keep only channel zero markers; scale and time are in the header. */
    for (unsigned y = 27; y < 480; y++) {
        for (unsigned x = 0; x < DISPLAY_PLOT_LEFT; x++) {
            framebuffer[y * 800 + x] = 0;
        }
        for (unsigned x = DISPLAY_PLOT_LEFT + DISPLAY_PLOT_WIDTH + 1; x < 800; x++) {
            framebuffer[y * 800 + x] = 0;
        }
        if (y > DISPLAY_PLOT_TOP + DISPLAY_PLOT_HEIGHT) {
            for (unsigned x = DISPLAY_PLOT_LEFT; x <= DISPLAY_PLOT_LEFT + DISPLAY_PLOT_WIDTH; x++) {
                framebuffer[y * 800 + x] = 0;
            }
        }
    }
    for (unsigned channel_index = 0; channel_index < 2; channel_index++) {
        int zero = 240 - scope_state.channels.position_millidivisions[channel_index] * 50 / 1000;
        if (scope_state.channels.visible[channel_index] && zero >= 40 && zero <= 440) {
            for (int arrow_offset_y = -3; arrow_offset_y <= 3; arrow_offset_y++) {
                for (int arrow_offset_x = (arrow_offset_y < 0 ? -arrow_offset_y : arrow_offset_y);
                     arrow_offset_x < 4;
                     arrow_offset_x++) {
                    framebuffer[(zero + arrow_offset_y) * 800 +
                                (DISPLAY_PLOT_LEFT + DISPLAY_PLOT_WIDTH + 1 + arrow_offset_x)] =
                        channel_index ? 0x07ff : 0xffe0;
                }
            }
        }
    }
    if (scope_state.trigger.enabled) {
        unsigned channel_index = scope_state.trigger.configured.source;
        int zero = 240 - scope_state.channels.position_millidivisions[channel_index] * 50 / 1000;
        int pixels =
            scope_state.trigger.configured.use_code
                ? (int)scope_ratio((int64_t)scope_state.trigger.configured.code * 25, 13) -
                      scope_state.channels.position_millidivisions[channel_index] * 50 / 1000
                : (int)scope_ratio(
                      (int64_t)scope_state.trigger.configured.level_uv * 50,
                      (uint64_t)scope_scale_mv(scope_state.channels.range[channel_index]) * 1000 *
                          scope_state.channels.probe_multiplier[channel_index]);
        int y = zero - pixels * (scope_state.channels.inverse[channel_index] ? -1 : 1);
        if (y < 40) {
            y = 40;
        }
        if (y > 440) {
            y = 440;
        }
        for (int arrow_offset_y = -4; arrow_offset_y <= 4; arrow_offset_y++) {
            for (int arrow_offset_x = (arrow_offset_y < 0 ? -arrow_offset_y : arrow_offset_y);
                 arrow_offset_x < 5;
                 arrow_offset_x++) {
                framebuffer[(y + arrow_offset_y) * 800 + DISPLAY_PLOT_LEFT + DISPLAY_PLOT_WIDTH +
                            11 + arrow_offset_x] = 0xffff;
            }
        }
    }
    hud_draw_status();
}

void display_annotations(void) {
    static unsigned previous[2][20];
    struct scope_voltage_cal cal0, cal1;
    scope_get_voltage_cal(0, scope_state.channels.range[0], &cal0);
    scope_get_voltage_cal(1, scope_state.channels.range[1], &cal1);
    unsigned keys[20] = {scope_state.channels.range[0],
                         scope_state.channels.range[1],
                         (unsigned)scope_state.channels.position_millidivisions[0],
                         (unsigned)scope_state.channels.position_millidivisions[1],
                         scope_state.channels.probe_multiplier[0],
                         scope_state.channels.probe_multiplier[1],
                         scope_state.channels.visible[0],
                         scope_state.channels.visible[1],
                         scope_state.display.view_start,
                         scope_state.display.view_points,
                         plot_rate_hz(),
                         cal0.valid | (cal1.valid << 2) | (scope_state.channels.inverse[0] << 4) |
                             (scope_state.channels.inverse[1] << 5),
                         scope_state.capture.live_enabled,
                         scope_state.trigger.enabled,
                         scope_state.trigger.configured.source,
                         scope_state.trigger.configured.slope,
                         scope_state.trigger.configured.sweep,
                         scope_state.trigger.configured.use_code,
                         (unsigned)scope_state.trigger.configured.code,
                         (unsigned)scope_state.trigger.configured.level_uv};
    unsigned axes = !scope_state.display.draw_buffer_valid,
             hud = !scope_state.display.draw_buffer_valid;
    for (unsigned i = 0; i < 20; i++) {
        if (keys[i] != previous[scope_state.display.draw_buffer_index][i]) {
            axes = 1;
        }
    }
    if (scope_state.display.display_redraw_requested) {
        axes = 1;
    }
    if (axes) {
        hud_draw_channel_markers();
    } else if (hud) {
        hud_draw_status();
    }
    for (unsigned i = 0; i < 20; i++) {
        previous[scope_state.display.draw_buffer_index][i] = keys[i];
    }
}

void hud_draw_measurements(void) {
    volatile uint16_t *framebuffer = scope_state.display.draw_framebuffer;
    for (unsigned y = 454; y < 480; y++) {
        for (unsigned x = DISPLAY_PLOT_LEFT; x < DISPLAY_PLOT_LEFT + DISPLAY_PLOT_WIDTH; x++) {
            framebuffer[y * 800 + x] = 0;
        }
    }
    for (unsigned channel = 0; channel < 2; channel++) {
        unsigned color = channel ? 0x07ff : 0xffe0;
        unsigned x = hud_text(channel ? 425 : 25, 458, channel ? "CH2 VPP: " : "CH1 VPP: ", color);
        unsigned maximum_x =
            hud_text(channel ? 425 : 25, 470, channel ? "CH2 MAX: " : "CH1 MAX: ", color);
        unsigned range = scope_state.frame.words[8 + channel];
        if (!scope_state.frame.valid_count || range >= 14 ||
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
