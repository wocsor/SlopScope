#include "spectrum.h"
#include "fft_tables.h"
#include "front_panel.h"
#include "panel_tools.h"
#include "scope_internal.h"

#define SPECTRUM_CODE __attribute__((section(".panel_text"), noinline))
static unsigned fft_source, fft_window = 3, fft_logarithmic = 1;
static unsigned fft_db_per_division = 10;
static int fft_reference_dbv = 20;
static int32_t fft_real[SCOPE_FFT_POINTS], fft_imaginary[SCOPE_FFT_POINTS];
static uint32_t fft_magnitudes[SCOPE_FFT_POINTS / 2 + 1];
static uint64_t fft_bin_multiplier_q20, fft_edge_multiplier_q20, fft_peak_uv;
static unsigned fft_ready, fft_rate_index, fft_peak_bin, fft_sequence, fft_duration_us;
static unsigned fft_input_source, fft_input_window, fft_input_probe, fft_input_gain;
static const char *const window_names[] = {
    "HAMMING", "RECTANGLE", "BLACKMAN", "HANN", "KAISER", "BARTLETT"};

unsigned panel_fft_source(void) {
    return fft_source;
}
unsigned panel_fft_window(void) {
    return fft_window;
}
unsigned panel_fft_logarithmic(void) {
    return fft_logarithmic;
}
unsigned panel_fft_db_per_division(void) {
    return fft_db_per_division;
}
int panel_fft_reference_dbv(void) {
    return fft_reference_dbv;
}
const char *panel_fft_window_name(void) {
    return window_names[fft_window];
}

SPECTRUM_CODE void panel_fft_action(unsigned action) {
    if (action == 1) {
        fft_source ^= 1;
    } else if (action == 2) {
        fft_window = (fft_window + 1) % 6;
    } else if (action == 3) {
        fft_logarithmic ^= 1;
    }
}

SPECTRUM_CODE void panel_fft_adjust(int steps) {
    int value = (int)fft_db_per_division + steps * 5;
    fft_db_per_division = value < 5 ? 5 : value > 20 ? 20 : value;
}

static SPECTRUM_CODE uint64_t spectrum_bin_uv(unsigned bin) {
    uint64_t multiplier =
        bin && bin != SCOPE_FFT_POINTS / 2 ? fft_bin_multiplier_q20 : fft_edge_multiplier_q20;
    return ((uint64_t)fft_magnitudes[bin] * multiplier) >> 20;
}

SPECTRUM_CODE int panel_fft_prepare(void) {
    unsigned channel = fft_source;
    unsigned range = scope_state.frame.words[8 + channel];
    if (!scope_state.frame.sequence || scope_state.frame.valid_count < SCOPE_FFT_POINTS ||
        range >= 14 || !frame_channel_matches_live_range(channel) ||
        scope_state.frame.calibration_ids[channel] != scope_adc_calibration_id(channel, range)) {
        fft_ready = 0;
        return 0;
    }
    uint32_t started = timer_ticks();
    struct scope_voltage_cal calibration;
    scope_get_voltage_cal(channel, range, &calibration);
    unsigned probe = scope_state.channels.probe_multiplier[channel];
    if (fft_ready && fft_sequence == scope_state.frame.sequence && fft_input_source == channel &&
        fft_input_window == fft_window && fft_input_probe == probe &&
        fft_input_gain == calibration.gain) {
        return 1;
    }
    fft_input_source = channel;
    fft_input_window = fft_window;
    fft_input_probe = probe;
    fft_input_gain = calibration.gain;
    /* Removing DC cancels the affine zero/DAC terms; retain the calibrated ADC gain. */
    uint64_t gain = (uint64_t)calibration.gain * probe;
    fft_edge_multiplier_q20 = scope_ratio((gain >> 3) * 32768, fft_window_mean_q15[fft_window]);
    fft_bin_multiplier_q20 = scope_ratio(fft_edge_multiplier_q20 * 46341, 32768);
    unsigned first =
        scope_state.frame.valid_start + scope_state.frame.valid_count - SCOPE_FFT_POINTS;
    const uint8_t *samples = (const uint8_t *)(scope_state.frame.words + 16) + channel * 2000 +
                             (channel ? scope_state.frame.channel_alignment : 0);
    int64_t sum = 0;
    for (unsigned index = 0; index < SCOPE_FFT_POINTS; index++) {
        fft_real[index] = (int8_t)samples[first + index] * 32768;
        sum += fft_real[index];
        if (!(index & 63)) {
            usb_service();
        }
    }
    int32_t mean = sum / 1024;
    const int16_t *window = fft_windows_q15[fft_window];
    for (unsigned index = 0; index < SCOPE_FFT_POINTS; index++) {
        fft_real[index] = ((int64_t)(fft_real[index] - mean) * window[index]) >> 15;
        fft_imaginary[index] = 0;
        if (!(index & 63)) {
            usb_service();
        }
    }
    scope_fft_transform(fft_real, fft_imaginary);
    fft_peak_bin = 1;
    for (unsigned bin = 0; bin <= SCOPE_FFT_POINTS / 2; bin++) {
        fft_magnitudes[bin] = scope_fft_magnitude(fft_real[bin], fft_imaginary[bin]);
        if (bin > 0 && fft_magnitudes[bin] > fft_magnitudes[fft_peak_bin]) {
            fft_peak_bin = bin;
        }
        if (!(bin & 15)) {
            usb_service();
        }
    }
    fft_peak_uv = spectrum_bin_uv(fft_peak_bin);
    fft_rate_index = scope_state.frame.words[14];
    fft_sequence = scope_state.frame.sequence;
    fft_duration_us = (started - timer_ticks()) / 24;
    fft_ready = 1;
    return 1;
}

SPECTRUM_CODE int panel_fft_y(unsigned column, uint64_t linear_scale_uv) {
    unsigned bin = column * (SCOPE_FFT_POINTS / 2) / (DISPLAY_PLOT_WIDTH - 1);
    uint64_t voltage_uv = spectrum_bin_uv(bin);
    int64_t y;
    if (fft_logarithmic) {
        y = DISPLAY_PLOT_TOP +
            scope_ratio((int64_t)(fft_reference_dbv * 1000 - scope_fft_db_milli(voltage_uv)) * 50,
                        fft_db_per_division * 1000);
    } else {
        y = DISPLAY_PLOT_TOP + DISPLAY_PLOT_HEIGHT -
            scope_ratio((int64_t)voltage_uv * 50, linear_scale_uv);
    }
    return y < DISPLAY_PLOT_TOP                         ? DISPLAY_PLOT_TOP
           : y > DISPLAY_PLOT_TOP + DISPLAY_PLOT_HEIGHT ? DISPLAY_PLOT_TOP + DISPLAY_PLOT_HEIGHT
                                                        : y;
}

static SPECTRUM_CODE unsigned spectrum_frequency(unsigned x, uint64_t millihz) {
    char text[28];
    hud_format_frequency(text, millihz);
    return hud_text(x, 28, text, 0xf81f);
}

SPECTRUM_CODE void panel_fft_readouts(void) {
    unsigned x = hud_text(160, 28, fft_source ? "AC FFT CH2 " : "AC FFT CH1 ", 0xf81f);
    if (!fft_ready) {
        hud_text(x, 28, "WAIT: 1024 SAMPLES", 0xf81f);
        return;
    }
    uint64_t rate_millihz = scope_ratio((int64_t)sample_rate_numerator(fft_rate_index) * 1000,
                                        sample_rate_denominator(fft_rate_index));
    x = spectrum_frequency(x, scope_ratio(rate_millihz, 2 * DISPLAY_GRID_COLUMNS));
    x = hud_text(x, 28, "/DIV PK:", 0xf81f);
    x = spectrum_frequency(x, scope_ratio((int64_t)rate_millihz * fft_peak_bin, SCOPE_FFT_POINTS));
    if (fft_logarithmic) {
        x = hud_text(x, 28, "  ", 0xf81f);
        x = hud_number(x, 28, fft_db_per_division, 0xf81f);
        x = hud_text(x, 28, "DB/DIV TOP:", 0xf81f);
        x = hud_number(x, 28, fft_reference_dbv, 0xf81f);
        hud_text(x, 28, "DBV", 0xf81f);
    } else {
        char text[28];
        hud_format_voltage(text, panel_math_scale_uv());
        x = hud_text(x, 28, "  ", 0xf81f);
        x = hud_text(x, 28, text, 0xf81f);
        hud_text(x, 28, "RMS/DIV", 0xf81f);
    }
}

static SPECTRUM_CODE int spectrum_parse(const uint8_t *text, unsigned length, int32_t *values) {
    unsigned start = 0;
    for (unsigned index = 0; index < 6; index++) {
        unsigned end = start;
        while (end < length && text[end] != ' ') {
            end++;
        }
        if (!command_parse_signed(text + start, end - start, &values[index]) ||
            (index == 5 ? end != length : end == length)) {
            return 0;
        }
        start = end + 1;
    }
    return 1;
}

SPECTRUM_CODE unsigned panel_fft_command(const uint8_t *command, unsigned length, uint8_t *reply) {
    if (length > 10 && command_equals(command, 10, "PANEL FFT ")) {
        int32_t values[6];
        if (!spectrum_parse(command + 10, length - 10, values) || values[0] < 1 || values[0] > 2 ||
            values[1] < 0 || values[1] > 5 || values[2] < 0 || values[2] > 1 || values[3] < 0 ||
            values[3] > 13 || values[4] < 5 || values[4] > 20 || values[5] < -120 ||
            values[5] > 120) {
            return reply_write_text(reply, "ERR invalid LCD FFT\n");
        }
        fft_source = values[0] - 1;
        fft_window = values[1];
        fft_logarithmic = values[2];
        fft_db_per_division = values[4];
        fft_reference_dbv = values[5];
        panel_math_select_fft(values[3]);
        front_panel_menu_refresh();
        return reply_write_text(reply, "OK LCD FFT selected\n");
    }
    if (!command_equals(command, length, "FFT STATUS")) {
        return 0;
    }
    unsigned values[] = {fft_ready,
                         fft_source,
                         fft_window,
                         fft_logarithmic,
                         fft_db_per_division,
                         (unsigned)fft_reference_dbv,
                         SCOPE_FFT_POINTS,
                         fft_peak_bin,
                         (unsigned)fft_peak_uv,
                         fft_sequence,
                         fft_duration_us};
    const char *names[] = {"FFT ready=",
                           " source=",
                           " window=",
                           " logarithmic=",
                           " db_div=",
                           " reference_dbv=",
                           " points=",
                           " peak_bin=",
                           " peak_uv=",
                           " sequence=",
                           " transform_us="};
    unsigned written = 0;
    for (unsigned index = 0; index < sizeof(values) / sizeof(values[0]); index++) {
        written += reply_write_text(reply + written, names[index]);
        written += reply_write_hex32(reply + written, values[index]);
    }
    reply[written++] = '\n';
    return written;
}
