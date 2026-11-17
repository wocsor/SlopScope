#ifndef SCOPE_CONTROLS_H
#define SCOPE_CONTROLS_H
#include <stdint.h>
struct scope_trigger {
    unsigned source, slope, sweep, coupling;
    int32_t level_uv;
    unsigned holdoff_ns, use_code;
    int32_t code;
};
int64_t scope_ratio(int64_t numerator, uint64_t denominator);
int scope_threshold(unsigned range,
                    unsigned probe,
                    int position_millidivisions,
                    const struct scope_trigger *trigger_config,
                    int *high_threshold,
                    int *low_threshold);
int scope_window_threshold(
    unsigned range, unsigned probe, int position_millidivisions, int32_t voltage_uv, int *code);
unsigned scope_trigger_word(const struct scope_trigger *trigger_config);
unsigned scope_holdoff_word(unsigned time_ns);
int scope_position_dac(unsigned channel_index,
                       unsigned range,
                       int position_millidivisions,
                       unsigned *dac_result);
unsigned scope_scale_mv(unsigned range);
#endif
