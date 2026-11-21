#ifndef SCOPE_STORAGE_H
#define SCOPE_STORAGE_H
#ifndef SCOPE_VOLATILE_SETTINGS
#define SCOPE_VOLATILE_SETTINGS 0
#endif
#include "acquisition.h"
#include "adc_gain.h"
#include "advanced.h"
#include "controls.h"
#include "features.h"
#include "voltage.h"
#include <stdint.h>
#define SCOPE_SETTINGS_SLOT0 0x517000u
#define SCOPE_SETTINGS_SLOT1 0x518000u
#define SCOPE_LEGACY_SETTINGS_SLOT0 0xffe000u
struct scope_saved_settings {
    uint32_t range[2], coupling[2];
    int32_t position[2];
    uint32_t probe[2], visible[2], inverse[2], rate, start, points, trigger_enabled, run;
    struct scope_trigger trigger;
    struct scope_voltage_cal cal[2][14];
    struct scope_advanced_trigger advanced;
    struct scope_acquisition_control acquisition;
    struct scope_features features;
    struct scope_adc_gain adc_gain[2][14];
    uint32_t voltage_baseline; /* 0 stock, 1 reference, 2 stock gain + saved zero */
    uint32_t light_mode;
    uint32_t brightness_percent;
};
int scope_saved_settings_valid(const struct scope_saved_settings *saved_settings);
int scope_flash_init(void);
int scope_flash_read(uint32_t address, void *destination_buffer, unsigned remaining_bytes);
unsigned scope_flash_id(void);
int scope_flash_unique_id(uint8_t unique_id[16]);
void scope_flash_release(void);
uint32_t scope_crc32(const void *source, unsigned byte_count);
int scope_assets_from_flash(void);
int scope_settings_load(struct scope_saved_settings *settings_result);
int scope_settings_save(const struct scope_saved_settings *settings_to_save);
unsigned scope_settings_sequence(void);
unsigned scope_settings_write_blocked(void);
unsigned scope_settings_need_migration(void);
#endif
