#ifndef SCOPE_PULSE_H
#define SCOPE_PULSE_H
#include <stdint.h>
/* Conservative sampled pulse qualification */
int scope_pulse_match_ratio(const volatile uint8_t *data,
                            unsigned count,
                            unsigned sample_rate_hz,
                            unsigned denominator,
                            int high,
                            int low,
                            unsigned positive,
                            unsigned comparison,
                            unsigned width_ns);
int scope_pulse_match(const volatile uint8_t *data,
                      unsigned count,
                      unsigned sample_rate_hz,
                      int high,
                      int low,
                      unsigned positive,
                      unsigned comparison,
                      unsigned width_ns);
#endif
