#ifndef SCOPE_VOLTAGE_H
#define SCOPE_VOLTAGE_H
#include <stdint.h>

struct scope_voltage_cal {
    int32_t zero;
    uint32_t gain, dac;
    int32_t slope;
    uint32_t valid;
};
struct scope_plot_config {
    int64_t zero;
    uint32_t gain;
    int32_t zero_y, inverse;
};
int scope_voltage_cal_valid(const struct scope_voltage_cal *voltage_calibration);
int scope_set_voltage_cal(unsigned channel_index,
                          unsigned range,
                          const struct scope_voltage_cal *voltage_calibration);
void scope_get_voltage_cal(unsigned channel_index,
                           unsigned range,
                           struct scope_voltage_cal *voltage_calibration);
void scope_get_reference_voltage_cal(unsigned channel_index,
                                     unsigned range,
                                     struct scope_voltage_cal *voltage_calibration);
unsigned scope_voltage_baseline(void);
int scope_voltage_baseline_set(unsigned mode);
void scope_voltage_position(unsigned channel_index, int position_millidivisions);
int scope_effective_position_dac(unsigned channel_index,
                                 unsigned range,
                                 int position_millidivisions,
                                 unsigned *dac_result);
int64_t scope_voltage_uv(const struct scope_voltage_cal *voltage_calibration,
                         int code,
                         unsigned offset_dac);
void scope_plot_configure(const struct scope_voltage_cal *voltage_calibration,
                          unsigned offset_dac,
                          unsigned display_range,
                          int position_millidivisions,
                          unsigned inverse,
                          struct scope_plot_config *plot_config);
int scope_plot_y(const struct scope_plot_config *plot_config, int code);
int scope_voltage_code(unsigned channel_index,
                       unsigned range,
                       unsigned probe,
                       unsigned offset_dac,
                       int32_t voltage_uv,
                       int *code);
#endif
