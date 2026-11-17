#include "voltage.h"
#include "adc_gain.h"
#include "controls.h"
#include "voltage_reference.h"
static unsigned reference_baseline;
static int stock_position[2];
unsigned scope_voltage_baseline(void) {
    return reference_baseline;
}
int scope_voltage_baseline_set(unsigned mode) {
    if (mode > 2) {
        return 0;
    }
    reference_baseline = mode;
    return 1;
}
void scope_voltage_position(unsigned channel_index, int position_millidivisions) {
    if (channel_index < 2) {
        stock_position[channel_index] = position_millidivisions;
    }
}
int scope_voltage_cal_valid(const struct scope_voltage_cal *voltage_calibration) {
    return voltage_calibration && voltage_calibration->valid <= 2 &&
           voltage_calibration->zero >= -(128 << 16) && voltage_calibration->zero <= (127 << 16) &&
           voltage_calibration->gain && voltage_calibration->gain <= 1024000000u &&
           voltage_calibration->dac <= 65535 && voltage_calibration->slope >= -65536 &&
           voltage_calibration->slope <= 65536;
}
int scope_set_voltage_cal(unsigned channel_index,
                          unsigned range,
                          const struct scope_voltage_cal *voltage_calibration) {
    if (channel_index > 1 || range >= 14 || !scope_voltage_cal_valid(voltage_calibration)) {
        return 0;
    }
    calibration[channel_index][range] = *voltage_calibration;
    return 1;
}
void scope_get_reference_voltage_cal(unsigned channel_index,
                                     unsigned range,
                                     struct scope_voltage_cal *voltage_calibration) {
    if (channel_index > 1 || range >= 14) {
        volatile uint32_t *calibration_words = (volatile uint32_t *)voltage_calibration;
        for (unsigned i = 0; i < 5; i++) {
            calibration_words[i] = 0;
        }
        return;
    }
    if (calibration[channel_index][range].valid) {
        *voltage_calibration = calibration[channel_index][range];
        return;
    }
    unsigned offset_dac = 0;
    scope_position_dac(channel_index, range, 0, &offset_dac);
    *voltage_calibration = (struct scope_voltage_cal){
        0, scope_scale_mv(range) * 10240u, offset_dac, 0, 0}; /* 25 nominal codes/div */
}
void scope_get_voltage_cal(unsigned channel_index,
                           unsigned range,
                           struct scope_voltage_cal *voltage_calibration) {
    if (reference_baseline == 1 || channel_index > 1 || range >= 14) {
        scope_get_reference_voltage_cal(channel_index, range, voltage_calibration);
        return;
    }
    if (reference_baseline == 2) {
        struct scope_adc_gain reference_gain, factory;
        scope_get_reference_voltage_cal(channel_index, range, voltage_calibration);
        scope_adc_gain_get_reference(channel_index, range, &reference_gain);

        if (voltage_calibration->valid && voltage_calibration->slope < 0 &&
            scope_adc_gain_factory(channel_index, range, &factory) &&
            reference_gain.scale == factory.scale && reference_gain.trim == factory.trim) {
            voltage_calibration->gain = scope_scale_mv(range) * 10240u;
            voltage_calibration->valid = 2;
            return;
        }
    }
    /* Stock processed samples are raw*16, at 400 processed codes/div */
    int pixels = stock_position[channel_index] * 50 / 1000;
    *voltage_calibration =
        (struct scope_voltage_cal){pixels * 32768, scope_scale_mv(range) * 10240u, 0, 0, 0};
}
int64_t scope_voltage_uv(const struct scope_voltage_cal *voltage_calibration,
                         int code,
                         unsigned offset_dac) {
    int64_t corrected =
        (int64_t)code * 65536 - voltage_calibration->zero -
        ((int)offset_dac - (int)voltage_calibration->dac) * (int64_t)voltage_calibration->slope;
    return corrected * voltage_calibration->gain / 16777216LL;
}
void scope_plot_configure(const struct scope_voltage_cal *voltage_calibration,
                          unsigned offset_dac,
                          unsigned display_range,
                          int position_millidivisions,
                          unsigned inverse,
                          struct scope_plot_config *plot_config) {
    plot_config->zero =
        voltage_calibration->zero +
        ((int)offset_dac - (int)voltage_calibration->dac) * (int64_t)voltage_calibration->slope;
    int64_t gain = scope_ratio((int64_t)voltage_calibration->gain * 50 * 256,
                               (uint64_t)scope_scale_mv(display_range) * 1000);
    plot_config->gain = gain > 0xffffffffLL ? 0xffffffffu : (unsigned)gain;
    plot_config->zero_y = 240 - position_millidivisions * 50 / 1000;
    plot_config->inverse = inverse ? -1 : 1;
}
int scope_plot_y(const struct scope_plot_config *plot_config, int code) {
    int64_t corrected = (int64_t)code * 65536 - plot_config->zero;
    if (corrected > 2147483647LL) {
        return plot_config->inverse > 0 ? 39 : 441;
    }
    if (corrected < -2147483647LL) {
        return plot_config->inverse > 0 ? 441 : 39;
    }
    int64_t delta = corrected * plot_config->gain / 4294967296LL;
    int64_t y = plot_config->zero_y - delta * plot_config->inverse;
    return y < 39 ? 39 : y > 441 ? 441 : (int)y;
}

int scope_effective_position_dac(unsigned channel_index,
                                 unsigned range,
                                 int position_millidivisions,
                                 unsigned *dac_result) {
    if (channel_index > 1 || range >= 14 || position_millidivisions < -4000 ||
        position_millidivisions > 4000) {
        return 0;
    }
    if (!reference_baseline) {
        return scope_position_dac(channel_index, range, position_millidivisions, dac_result);
    }
    struct scope_voltage_cal voltage_calibration;
    scope_get_voltage_cal(channel_index, range, &voltage_calibration);
    if (!voltage_calibration.valid || !voltage_calibration.slope) {
        return scope_position_dac(channel_index, range, position_millidivisions, dac_result);
    }
    int64_t desired =
        scope_ratio((int64_t)position_millidivisions * scope_scale_mv(range) * 16777216LL,
                    voltage_calibration.gain);
    int64_t delta = scope_ratio(
        (desired - voltage_calibration.zero) * (voltage_calibration.slope < 0 ? -1 : 1),
        voltage_calibration.slope < 0 ? -voltage_calibration.slope : voltage_calibration.slope);
    int64_t offset_dac = (int64_t)voltage_calibration.dac + delta;
    if (offset_dac < 0 || offset_dac > 65535) {
        return 0;
    }
    *dac_result = (unsigned)offset_dac;
    return 1;
}

/* Inverse of the same measured transfer used by LCD and host voltage readout. */
int scope_voltage_code(unsigned channel_index,
                       unsigned range,
                       unsigned probe,
                       unsigned offset_dac,
                       int32_t voltage_uv,
                       int *code) {
    if (channel_index > 1 || range >= 14 || !probe || offset_dac > 65535 || !code) {
        return 0;
    }
    struct scope_voltage_cal voltage_calibration;
    scope_get_voltage_cal(channel_index, range, &voltage_calibration);
    if (!voltage_calibration.gain) {
        return 0;
    }
    int64_t input = scope_ratio(voltage_uv, probe);
    int64_t corrected = scope_ratio(input * 16777216LL, voltage_calibration.gain);
    int64_t value = scope_ratio(corrected + voltage_calibration.zero +
                                    ((int)offset_dac - (int)voltage_calibration.dac) *
                                        (int64_t)voltage_calibration.slope,
                                65536);
    *code = value < -127 ? -127 : value > 126 ? 126 : (int)value;
    return 1;
}
