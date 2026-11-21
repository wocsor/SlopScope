#include "panel_tools.h"
#include "scope_internal.h"

struct scope_signal_summary scope_summarize_channel(unsigned channel) {
    uint16_t histogram[256] = {0};
    unsigned start = scope_state.frame.valid_start;
    unsigned end = start + scope_state.frame.valid_count;
    if (end > 1998) {
        end = 1998;
    }
    struct scope_signal_summary signal = {0};
    unsigned range = scope_state.frame.words[8 + channel];
    if (end <= start + 12 || !frame_channel_matches_live_range(channel) ||
        scope_state.frame.calibration_ids[channel] != scope_adc_calibration_id(channel, range)) {
        return signal;
    }
    const int8_t *samples = (const int8_t *)(scope_state.frame.words + 16) + channel * 2000 +
                            (channel ? scope_state.frame.channel_alignment : 0);
    int64_t sum = 0, square_sum = 0;
    signal.minimum_code = 127;
    signal.maximum_code = -128;
    signal.count = end - start;
    for (unsigned index = start; index < end; index++) {
        int sample = samples[index];
        sum += sample;
        square_sum += sample * sample;
        if (sample < signal.minimum_code) {
            signal.minimum_code = sample;
        }
        if (sample > signal.maximum_code) {
            signal.maximum_code = sample;
        }
        histogram[samples[index] + 128]++;
        signal.clipped |= samples[index] <= -124 || samples[index] >= 124;
        if (!(index & 255)) {
            usb_service();
        }
    }
    signal.mean_code_q8 = scope_ratio(sum * 256, signal.count);
    unsigned variance_q16 = scope_ratio((square_sum * signal.count - sum * sum) * 65536,
                                        (uint64_t)signal.count * signal.count);
    unsigned root = 0;
    for (unsigned bit = 1u << 30; bit; bit >>= 2) {
        if (variance_q16 >= root + bit) {
            variance_q16 -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
    }
    signal.ac_rms_code_q8 = root;
    unsigned population = 0, low = 0, high = 255;
    for (unsigned code = 0; code < 256; code++) {
        population += histogram[code];
        if (population < (end - start) / 10) {
            low = code + 1;
        }
        if (population >= (end - start) * 9 / 10) {
            high = code;
            break;
        }
    }
    signal.amplitude = high - low;
    if (signal.amplitude < 12) {
        return signal;
    }
    int lower = (int)low - 128 + signal.amplitude / 4;
    int upper = (int)high - 128 - signal.amplitude / 4;
    unsigned armed = 0, first_edge = 0, last_edge = 0, intervals = 0, previous_period = 0;
    unsigned consistent = 1;
    for (unsigned index = start; index < end; index++) {
        if (samples[index] <= lower) {
            armed = 1;
        } else if (armed && samples[index] >= upper) {
            armed = 0;
            if (last_edge) {
                unsigned period = index - last_edge;
                unsigned difference =
                    period > previous_period ? period - previous_period : previous_period - period;
                if (period < 8 || (previous_period && difference > previous_period / 4 + 1)) {
                    consistent = 0;
                }
                previous_period = period;
                intervals++;
            } else {
                first_edge = index;
            }
            last_edge = index;
        }
    }
    if (consistent && intervals >= 2) {
        signal.period_samples = (last_edge - first_edge) / intervals;
    }
    return signal;
}
