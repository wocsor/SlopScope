#include "acquisition.h"
#include "controls.h"
#include "tables.h"
void scope_acquisition_defaults(struct scope_acquisition_control *acquisition_config) {
    *acquisition_config = (struct scope_acquisition_control){10000, 0, 0};
}
int scope_acquisition_valid(const struct scope_acquisition_control *acquisition_config) {
    return acquisition_config &&
           (acquisition_config->record_depth == 1000 ||
            acquisition_config->record_depth == 10000) &&
           acquisition_config->horizontal_enabled < 2 &&
           acquisition_config->horizontal_ns >= -1000000000 &&
           acquisition_config->horizontal_ns <= 1000000000;
}
int scope_acquisition_geometry(unsigned sample_rate_hz,
                               unsigned depth,
                               int32_t time_ns,
                               struct scope_acquisition_geometry *geometry) {
    return scope_acquisition_geometry_ratio(sample_rate_hz, 1, depth, time_ns, geometry);
}
int scope_acquisition_geometry_ratio(unsigned sample_rate_hz,
                                     unsigned denominator,
                                     unsigned depth,
                                     int32_t time_ns,
                                     struct scope_acquisition_geometry *geometry) {
    if (!geometry || !sample_rate_hz || !denominator || (depth != 1000 && depth != 10000)) {
        return 0;
    }
    unsigned total_samples = depth + (48 - (depth / 2) % 48) * 2;
    int64_t horizontal_shift_samples =
        scope_ratio((int64_t)time_ns * sample_rate_hz, (uint64_t)denominator * 1000000000u);
    int64_t pretrigger_samples = (int64_t)(total_samples / 2) - horizontal_shift_samples;
    if (pretrigger_samples < 0 || pretrigger_samples > total_samples) {
        return 0;
    }
    unsigned phase_step_samples = (unsigned)scope_ratio((int64_t)sample_rate_hz * 8,
                                                        (uint64_t)denominator * 1000000000u),
             phase = 0;
    if (phase_step_samples) {
        unsigned remainder =
            (unsigned)pretrigger_samples -
            (unsigned)scope_ratio(pretrigger_samples, phase_step_samples) * phase_step_samples;
        phase = remainder ? remainder - 1 : phase_step_samples - 1;
    }
    *geometry = (struct scope_acquisition_geometry){depth,
                                                    (unsigned)pretrigger_samples,
                                                    total_samples - (unsigned)pretrigger_samples,
                                                    depth > 7600 ? depth / 2 - 3800 : 0,
                                                    phase};
    return 1;
}

int scope_live_acquisition_geometry(unsigned rate_index,
                                    int32_t time_ns,
                                    struct scope_acquisition_geometry *geometry) {
    if (!geometry || !rate_index || rate_index >= 32) {
        return 0;
    }
    unsigned plan_index = rate_index > 18 ? 18 : rate_index;
    unsigned pretrigger = 0, posttrigger = 0, address = 0;
    for (unsigned index = 0; index < rate_plan_counts[plan_index]; index++) {
        unsigned reg = rate_plans[plan_index][index][0];
        unsigned value = rate_plans[plan_index][index][1];
        if (reg == 0x14c) {
            pretrigger = value;
        } else if (reg == 0x148) {
            posttrigger = value;
        } else if (reg == 0x200) {
            address = value;
        }
    }
    unsigned total_samples = pretrigger + posttrigger;
    unsigned numerator = sample_rate_numerator(rate_index);
    unsigned denominator = sample_rate_denominator(rate_index);
    int64_t shift_samples =
        scope_ratio((int64_t)time_ns * numerator, (uint64_t)denominator * 1000000000u);
    int64_t shifted_pretrigger = (int64_t)pretrigger - shift_samples;
    if (!total_samples || shifted_pretrigger < 0 || shifted_pretrigger > total_samples) {
        return 0;
    }
    unsigned phase_step_samples =
        scope_ratio((int64_t)numerator * 8, (uint64_t)denominator * 1000000000u);
    unsigned phase = 0;
    if (phase_step_samples) {
        unsigned remainder = (unsigned)shifted_pretrigger % phase_step_samples;
        phase = remainder ? remainder - 1 : phase_step_samples - 1;
    }
    *geometry = (struct scope_acquisition_geometry){
        10000, (unsigned)shifted_pretrigger, total_samples - shifted_pretrigger, address, phase};
    return 1;
}
