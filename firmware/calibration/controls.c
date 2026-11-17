
#include "controls.h"
#include "control_calibration.h"
#include "factory.h"
static const unsigned voltage_scales_mv[] = {
    1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
unsigned scope_scale_mv(unsigned range) {
    return range < 14 ? voltage_scales_mv[range] : 0;
}
int64_t scope_ratio(int64_t numerator, uint64_t denominator) {
    if (!denominator) {
        return 0;
    }
    unsigned negative = numerator < 0;
    uint64_t absolute_numerator = negative ? 0 - (uint64_t)numerator : (uint64_t)numerator,
             quotient = 0, remainder = 0;
    for (unsigned i = 0; i < 64; i++) {
        remainder = (remainder << 1) | (absolute_numerator >> 63);
        absolute_numerator <<= 1;
        quotient <<= 1;
        if (remainder >= denominator) {
            remainder -= denominator;
            quotient |= 1;
        }
    }
    if (negative && quotient == 0x8000000000000000ULL) {
        return INT64_MIN;
    }
    return negative ? -(int64_t)quotient : (int64_t)quotient;
}
static int clamp_adc_threshold(int x) {
    return x < -127 ? -127 : x > 126 ? 126 : x;
}
int scope_threshold(unsigned range,
                    unsigned probe,
                    int position_millidivisions,
                    const struct scope_trigger *trigger_config,
                    int *high_threshold,
                    int *low_threshold) {
    if (range >= 14 || !probe || trigger_config->source > 1 || trigger_config->slope > 1 ||
        trigger_config->sweep > 2 || trigger_config->coupling > 1) {
        return 0;
    }
    if (trigger_config->use_code) {
        *high_threshold =
            clamp_adc_threshold(trigger_config->code + (trigger_config->slope ? 4 : 0));
        *low_threshold =
            clamp_adc_threshold(trigger_config->code - (trigger_config->slope ? 0 : 4));
        return 1;
    }
    int pixels = (int)scope_ratio((int64_t)trigger_config->level_uv * 50,
                                  (uint64_t)voltage_scales_mv[range] * 1000 * probe) +
                 (int)scope_ratio((int64_t)position_millidivisions * 50, 1000);
    *high_threshold = clamp_adc_threshold(
        (int)scope_ratio((int64_t)(pixels + (trigger_config->slope ? 14 : 0)) * 13, 25));
    *low_threshold = clamp_adc_threshold(
        (int)scope_ratio((int64_t)(pixels - (trigger_config->slope ? 0 : 14)) * 13, 25));
    return 1;
}
unsigned scope_trigger_word(const struct scope_trigger *trigger_config) {
    return 0x200000u | (trigger_config->slope ? 0x100001u : 0) | (trigger_config->source << 14) |
           (trigger_config->sweep << 12);
}
/* Stock e2de8/dd1a4/e5b50 use half a code/pixel, unlike Edge's 13/25 */
int scope_window_threshold(
    unsigned range, unsigned probe, int position_millidivisions, int32_t voltage_uv, int *code) {
    if (range >= 14 || !probe || !code) {
        return 0;
    }
    uint64_t denominator = (uint64_t)voltage_scales_mv[range] * 1000 * probe;
    int pixels = position_millidivisions * 50 / 1000;
    *code =
        (int)scope_ratio((int64_t)voltage_uv * 50 + (int64_t)denominator * pixels, denominator * 2);
    return 1;
}
unsigned scope_holdoff_word(unsigned time_ns) {
    unsigned timer_mantissa = time_ns >> 3, timer_exponent = 0;
    while (timer_mantissa >= 1000) {
        timer_mantissa /= 10;
        timer_exponent++;
    }
    return (timer_mantissa << 6) | timer_exponent;
}

/* Stock model initializer 1800c sets orientation = -1 */
int scope_position_dac(unsigned channel_index,
                       unsigned range,
                       int position_millidivisions,
                       unsigned *dac_result) {
    if (channel_index > 1 || range >= 14 || position_millidivisions < -4000 ||
        position_millidivisions > 4000) {
        return 0;
    }
    int zero = position_zero[channel_index][range], slope = position_slope[channel_index][range];
    scope_factory_position(channel_index, range, &zero, &slope);
    if (!slope && position_millidivisions) {
        return 0;
    }
    int pixels = position_millidivisions * 50 / 1000;
    int64_t value = zero - scope_ratio((int64_t)slope * pixels, 150);
    if (value < 0 || value > 65535) {
        return 0;
    }
    *dac_result = (unsigned)value;
    return 1;
}
