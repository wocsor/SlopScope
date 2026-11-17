#ifndef SCOPE_FACTORY_H
#define SCOPE_FACTORY_H
#include <stdint.h>
/* Boot-only read from this device's stock LittleFS. No write/format interface. */
int scope_factory_load(void);
unsigned scope_factory_status(void);
unsigned scope_factory_crc(void);
const uint8_t *scope_factory_table(unsigned *length);
unsigned scope_factory_phase(unsigned channel_index);
int scope_factory_position(unsigned channel_index, unsigned range, int *zero, int *slope);
unsigned scope_factory_adc(unsigned channel_index, unsigned range, unsigned fallback);
unsigned scope_factory_adc_trim(unsigned channel_index, unsigned range, unsigned fallback);
#endif
