#ifndef SCOPE_ADC_GAIN_H
#define SCOPE_ADC_GAIN_H
#include "voltage.h"
#include <stdint.h>
/* Opaque ADC words: five full-scale registers and the trim register */
struct scope_adc_gain {
    uint32_t scale, trim;
};
struct scope_adc_gain_transfer {
    unsigned dac;
    int old_low, old_high, new_low, new_high;
};
void scope_adc_gain_defaults(struct scope_adc_gain gain_table[2][14]);
int scope_adc_gain_valid(const struct scope_adc_gain *gain_settings);
void scope_adc_gain_get(unsigned channel_index,
                        unsigned range_index,
                        struct scope_adc_gain *gain_result);
void scope_adc_gain_get_reference(unsigned channel_index,
                                  unsigned range_index,
                                  struct scope_adc_gain *gain_result);
int scope_adc_gain_set(unsigned channel_index,
                       unsigned range_index,
                       const struct scope_adc_gain *gain_settings);
void scope_adc_gain_legacy(unsigned channel_index,
                           unsigned range_index,
                           struct scope_adc_gain *gain_result);
int scope_adc_gain_factory(unsigned channel_index,
                           unsigned range_index,
                           struct scope_adc_gain *gain_result);
uint32_t scope_adc_calibration_id(unsigned channel_index, unsigned range);
uint32_t scope_adc_gain_command(unsigned channel_index, unsigned range_index, uint32_t command);
int scope_adc_gain_migrate_calibration(const struct scope_voltage_cal *previous_calibration,
                                       const struct scope_adc_gain_transfer *gain_transfers,
                                       unsigned count,
                                       struct scope_voltage_cal *migrated_calibration);
#endif
