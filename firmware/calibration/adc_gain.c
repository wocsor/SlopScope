/* Frozen settings-v5 defaults, extracted from channel_plans.h for both channels */
#include "adc_gain.h"
#include "controls.h"
#include "factory.h"
#include "voltage.h"
extern uint32_t scope_crc32(const void *, unsigned);
static const struct scope_adc_gain legacy_gain_defaults[2][14] = {
    {{0x701c, 0xb},
     {0xe797, 0xe594},
     {0x100, 0x1102},
     {0x66e, 0x2dce},
     {0x66e, 0x2dce},
     {0x67e, 0x2fce},
     {0x64d, 0x29ae},
     {0x64d, 0x29ae},
     {0x66d, 0x2dae},
     {0x64c, 0x298e},
     {0x62e, 0x25ce},
     {0x66d, 0x2dae},
     {0xe1a0, 0xe350},
     {0xe92d, 0xe59f}},
    {{0xe280, 0xe3a0},
     {0xe8bd, 0xe8bd},
     {0x340, 0x1342},
     {0x65f, 0x2bee},
     {0x620, 0x240e},
     {0x67f, 0x2fee},
     {0x630, 0x260e},
     {0x61f, 0x23ee},
     {0x66e, 0x2dce},
     {0x62f, 0x25ee},
     {0x65e, 0x2bce},
     {0x66e, 0x2dce},
     {0xe58d, 0xe59f},
     {0x1208, 0x1005}},
};
static struct scope_adc_gain reference_gain_table[2][14];
static unsigned reference_gain_initialized;
void scope_adc_gain_defaults(struct scope_adc_gain gain_table[2][14]) {
    for (unsigned channel_index = 0; channel_index < 2; channel_index++) {
        for (unsigned range_index = 0; range_index < 14; range_index++) {
            gain_table[channel_index][range_index] =
                legacy_gain_defaults[channel_index][range_index];
        }
    }
}
static void adc_gain_initialize_reference(void) {
    if (!reference_gain_initialized) {
        scope_adc_gain_defaults(reference_gain_table);
        reference_gain_initialized = 1;
    }
}
int scope_adc_gain_valid(const struct scope_adc_gain *gain_settings) {
    return gain_settings && gain_settings->scale <= 65535 && gain_settings->trim <= 65535;
}
void scope_adc_gain_get_reference(unsigned channel_index,
                                  unsigned range_index,
                                  struct scope_adc_gain *gain_result) {
    adc_gain_initialize_reference();
    *gain_result = channel_index < 2 && range_index < 14
                       ? reference_gain_table[channel_index][range_index]
                       : (struct scope_adc_gain){0, 0};
}
void scope_adc_gain_get(unsigned channel_index,
                        unsigned range_index,
                        struct scope_adc_gain *gain_result) {
    if (scope_voltage_baseline() != 1 &&
        scope_adc_gain_factory(channel_index, range_index, gain_result)) {
        return;
    }
    scope_adc_gain_get_reference(channel_index, range_index, gain_result);
}
int scope_adc_gain_set(unsigned channel_index,
                       unsigned range_index,
                       const struct scope_adc_gain *gain_settings) {
    if (channel_index > 1 || range_index >= 14 || !scope_adc_gain_valid(gain_settings)) {
        return 0;
    }
    adc_gain_initialize_reference();
    reference_gain_table[channel_index][range_index] = *gain_settings;
    return 1;
}
void scope_adc_gain_legacy(unsigned channel_index,
                           unsigned range_index,
                           struct scope_adc_gain *gain_result) {
    *gain_result = channel_index < 2 && range_index < 14
                       ? legacy_gain_defaults[channel_index][range_index]
                       : (struct scope_adc_gain){0, 0};
}
int scope_adc_gain_factory(unsigned channel_index,
                           unsigned range_index,
                           struct scope_adc_gain *gain_result) {
    if (!gain_result || channel_index > 1 || range_index < 3 || range_index > 11 ||
        scope_factory_status() < 1 || scope_factory_status() > 2) {
        return 0;
    }
    *gain_result = (struct scope_adc_gain){scope_factory_adc(channel_index, range_index, 0),
                                           scope_factory_adc_trim(channel_index, range_index, 0)};
    return scope_adc_gain_valid(gain_result);
}
uint32_t scope_adc_gain_command(unsigned channel_index, unsigned range_index, uint32_t command) {
    if (channel_index > 1 || range_index >= 14) {
        return command;
    }
    struct scope_adc_gain gain_settings;
    scope_adc_gain_get(channel_index, range_index, &gain_settings);
    unsigned register_address = (command >> 16) & 255;
    if (register_address == 2 || register_address == 5 || register_address == 8 ||
        register_address == 11 || register_address == 14) {
        return (command & 0xffff0000u) | gain_settings.scale;
    }
    if (register_address == 17) {
        return (command & 0xffff0000u) | gain_settings.trim;
    }
    return command;
}

uint32_t scope_adc_calibration_id(unsigned channel_index, unsigned range) {
    struct scope_voltage_cal voltage_calibration;
    struct scope_adc_gain gain;
    scope_get_voltage_cal(channel_index, range, &voltage_calibration);
    scope_adc_gain_get(channel_index, range, &gain);
    uint32_t identity[7] = {(uint32_t)voltage_calibration.zero,
                            voltage_calibration.gain,
                            voltage_calibration.dac,
                            (uint32_t)voltage_calibration.slope,
                            voltage_calibration.valid,
                            gain.scale,
                            gain.trim};
    return scope_crc32(identity, sizeof(identity));
}

/* Convert a voltage calibration through a measured affine ADC-code map */
int scope_adc_gain_migrate_calibration(const struct scope_voltage_cal *previous_calibration,
                                       const struct scope_adc_gain_transfer *gain_transfers,
                                       unsigned count,
                                       struct scope_voltage_cal *migrated_calibration) {
    if (!previous_calibration || !gain_transfers || !migrated_calibration || count < 1 ||
        count > 2 || !scope_voltage_cal_valid(previous_calibration)) {
        return 0;
    }
    int64_t gain_ratios_q16[2], zero_offsets_q16[2];
    for (unsigned i = 0; i < count; i++) {
        if (gain_transfers[i].old_low < -127 || gain_transfers[i].old_high > 126 ||
            gain_transfers[i].new_low < -127 || gain_transfers[i].new_high > 126 ||
            gain_transfers[i].old_high - gain_transfers[i].old_low < 8 ||
            gain_transfers[i].new_high - gain_transfers[i].new_low < 8) {
            return 0;
        }
        gain_ratios_q16[i] =
            scope_ratio((int64_t)(gain_transfers[i].old_high - gain_transfers[i].old_low) * 65536,
                        (unsigned)(gain_transfers[i].new_high - gain_transfers[i].new_low));
        if (gain_ratios_q16[i] < 32768 || gain_ratios_q16[i] > 131072) {
            return 0;
        }
    }
    int64_t gain_ratio_q16 = gain_ratios_q16[0];
    if (count == 2) {
        int64_t delta = gain_ratios_q16[1] - gain_ratios_q16[0];
        if (delta < 0) {
            delta = -delta;
        }
        int64_t mean = (gain_ratios_q16[0] + gain_ratios_q16[1]) / 2;
        if (delta * 100 > mean * 8) {
            return 0;
        }
        if (gain_transfers[0].dac > 65535 || gain_transfers[1].dac > 65535 ||
            gain_transfers[0].dac == gain_transfers[1].dac) {
            return 0;
        }
        unsigned dac_delta = gain_transfers[0].dac > gain_transfers[1].dac
                                 ? gain_transfers[0].dac - gain_transfers[1].dac
                                 : gain_transfers[1].dac - gain_transfers[0].dac;
        if (dac_delta < 128) {
            return 0;
        }
        gain_ratio_q16 = mean;
    }
    for (unsigned i = 0; i < count; i++) {
        zero_offsets_q16[i] =
            (int64_t)gain_transfers[i].old_low * 65536 - gain_ratio_q16 * gain_transfers[i].new_low;
    }
    int64_t offset_slope_q16 = 0;
    if (count == 2) {
        int64_t offset_difference_q16 = zero_offsets_q16[1] - zero_offsets_q16[0];
        if (gain_transfers[1].dac < gain_transfers[0].dac) {
            offset_difference_q16 = -offset_difference_q16;
        }
        unsigned span = gain_transfers[1].dac > gain_transfers[0].dac
                            ? gain_transfers[1].dac - gain_transfers[0].dac
                            : gain_transfers[0].dac - gain_transfers[1].dac;
        offset_slope_q16 = scope_ratio(offset_difference_q16, span);
    }
    int64_t offset_at_calibration_q16 =
        zero_offsets_q16[0] +
        offset_slope_q16 * ((int64_t)previous_calibration->dac - gain_transfers[0].dac);
    int64_t zero =
        scope_ratio(((int64_t)previous_calibration->zero - offset_at_calibration_q16) * 65536,
                    (uint64_t)gain_ratio_q16);
    int64_t gain = scope_ratio((int64_t)previous_calibration->gain * gain_ratio_q16, 65536);
    int64_t slope = scope_ratio(((int64_t)previous_calibration->slope - offset_slope_q16) * 65536,
                                (uint64_t)gain_ratio_q16);
    if (zero < INT32_MIN || zero > INT32_MAX || slope < INT32_MIN || slope > INT32_MAX ||
        gain <= 0 || gain > UINT32_MAX) {
        return 0;
    }
    *migrated_calibration = (struct scope_voltage_cal){(int32_t)zero,
                                                       (uint32_t)gain,
                                                       previous_calibration->dac,
                                                       (int32_t)slope,
                                                       previous_calibration->valid};
    return scope_voltage_cal_valid(migrated_calibration);
}
