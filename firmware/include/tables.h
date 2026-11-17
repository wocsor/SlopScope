#ifndef SCOPE_TABLES_H
#define SCOPE_TABLES_H
#include "board.h"
#include <stdint.h>
extern const uint16_t boot_logo_palette[256];
extern const uint8_t boot_logo_pixels[22560];
extern const unsigned int selftest_setup[41][2];
extern const unsigned int sample_rates[32][4];
unsigned sample_rate_denominator(unsigned rate);
unsigned sample_rate_numerator(unsigned rate);
extern const unsigned int rate_plan_counts[19];
extern const unsigned int rate_plans[19][24][2];
extern const unsigned int roll_plans[3][24][2];
extern const unsigned int adc_setup[191][3];
extern const unsigned int adc_normal_setup[17][3];
extern const unsigned int analog_setup[146][3];
extern const unsigned int channel_plan_counts[2][14];
extern const unsigned int channel_plans[2][14][45][3];
#endif
