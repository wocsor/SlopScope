#include "pulse.h"
static int pulse_width_qualifies(unsigned span,
                                 unsigned sample_rate_hz,
                                 unsigned denominator,
                                 unsigned comparison,
                                 unsigned width,
                                 unsigned complete) {
    if (!span) {
        return 0;
    }
    uint64_t lower = (uint64_t)(span - 1) * 1000000000u * denominator,
             upper = (uint64_t)(span + 1) * 1000000000u * denominator;
    uint64_t target = (uint64_t)width * sample_rate_hz;
    if (comparison == 0) {
        return lower > target;
    }
    if (!complete) {
        return 0;
    }
    if (comparison == 2) {
        return upper < target;
    }
    unsigned low_ns = (width / 100) * 95 + (width % 100) * 95 / 100,
             high_ns = (width / 100) * 105 + (width % 100) * 105 / 100;
    uint64_t minimum = (uint64_t)low_ns * sample_rate_hz,
             maximum = (uint64_t)high_ns * sample_rate_hz;
    return lower >= minimum && upper <= maximum;
}
int scope_pulse_match(const volatile uint8_t *data,
                      unsigned count,
                      unsigned sample_rate_hz,
                      int high,
                      int low,
                      unsigned positive,
                      unsigned comparison,
                      unsigned width_ns) {
    return scope_pulse_match_ratio(
        data, count, sample_rate_hz, 1, high, low, positive, comparison, width_ns);
}
int scope_pulse_match_ratio(const volatile uint8_t *data,
                            unsigned count,
                            unsigned sample_rate_hz,
                            unsigned denominator,
                            int high,
                            int low,
                            unsigned positive,
                            unsigned comparison,
                            unsigned width_ns) {
    if (!data || count < 3 || !sample_rate_hz || !denominator || high <= low || comparison > 2 ||
        !width_ns) {
        return 0;
    }
    int enter = positive ? high : -low, leave = positive ? low : -high;
    int first = (int)(int8_t)data[0];
    if (!positive) {
        first = -first;
    }
    unsigned active = first >= enter, inactive_seen = first <= leave, begin = 0, has_start = 0;
    for (unsigned i = 1; i < count; i++) {
        int value = (int)(int8_t)data[i];
        if (!positive) {
            value = -value;
        }
        if (!active) {
            if (value <= leave) {
                inactive_seen = 1;
            }
            if (value >= enter) {
                active = 1;
                begin = i;
                has_start = inactive_seen;
                inactive_seen = 0;
            }
        } else if (value <= leave) {
            if (pulse_width_qualifies(
                    i - begin, sample_rate_hz, denominator, comparison, width_ns, has_start)) {
                return 1;
            }
            active = 0;
            inactive_seen = 1;
            has_start = 0;
        }
    }
    return active && pulse_width_qualifies(
                         count - 1 - begin, sample_rate_hz, denominator, comparison, width_ns, 0);
}
