#ifndef SCOPE_INTERNAL_H
#define SCOPE_INTERNAL_H
#include "board.h"
#include "boot_logo_dimensions.h"
#include "capture_registers.h"
#include "controls.h"
#include "factory.h"
#include "platform.h"
#include "protocol.h"
#include "pulse.h"
#include "range_plan.h"
#include "scope_state.h"
#include "storage.h"
#include "tables.h"
#include "voltage.h"
#include <stddef.h>
#include <stdint.h>

void spi1_select(void);
void spi1_deselect(void);
int spi1_initialize(void);
int spi1_transfer(const void *transmit_data, void *receive_data, int byte_count);
void fpga_prepare_gpio(void);
void fpga_configuration_poll(void);
int command_equals(const uint8_t *command, unsigned length, const char *expected_command);
unsigned reply_write_text(uint8_t *reply, const char *text);
unsigned reply_write_hex32(uint8_t *reply, uint32_t value);
unsigned capture_alignment(void);
void publish_frame(void);
const uint8_t *scope_snapshot_legacy(unsigned *length);
const uint8_t *scope_snapshot_v2(unsigned *length);
const uint8_t *scope_snapshot_v3(unsigned *length);
const uint8_t *scope_snapshot_v4(unsigned *length);
const uint8_t *scope_snapshot_v5(unsigned *length);
const uint8_t *scope_snapshot_v6(unsigned *length);
const uint8_t *scope_snapshot_v7(unsigned *length);
int fpga_write_register(unsigned address, unsigned value);
int fpga_read_register(unsigned address, uint32_t *value);
void capture_stop(void);
void capture_abort_wait(void);
uint32_t *record_buffer(unsigned channel);
const uint8_t *scope_record_channel_snapshot(unsigned channel, unsigned *length);
const uint8_t *scope_record_snapshot(unsigned *length);
void publish_record(void);
int record_read_begin(void);
void trigger_bank_store(void);
void video_control_normalize(struct scope_trigger *trigger_config,
                             struct scope_advanced_trigger *advanced_trigger);
int trigger_program(void);
unsigned hud_text(unsigned x, unsigned y, const char *text, unsigned color);
unsigned hud_number(unsigned x, unsigned y, int value, unsigned color);
void display_annotations(void);
void hud_draw_usb_status(void);
void hud_draw_measurements(void);
void activity_poll(void);
void display_usb_activity_poll(void);
void capture_start(void);
int capture_busy(void);
void live_poll(void);
void display_poll(void);
void capture_poll(void);
void adc_poll(void);
unsigned adc_command(const uint8_t *command, unsigned length, uint8_t *reply);
void analog_poll(void);
unsigned analog_command(const uint8_t *command, unsigned length, uint8_t *reply);
unsigned capture_command(const uint8_t *command, unsigned length, uint8_t *reply);
int command_parse_unsigned(const uint8_t *text, unsigned length, unsigned *value);
unsigned channel_command(const uint8_t *command, unsigned length, uint8_t *reply);
int command_parse_signed(const uint8_t *text, unsigned length, int32_t *value);
unsigned capture_begin_live(unsigned intent, uint8_t *reply);
unsigned trigger_command(const uint8_t *command, unsigned length, uint8_t *reply);
unsigned view_command(const uint8_t *command, unsigned length, uint8_t *reply);
unsigned record_command(const uint8_t *command, unsigned length, uint8_t *reply);
int scope_saved_settings_valid(const struct scope_saved_settings *saved_settings);
void settings_capture(struct scope_saved_settings *saved_settings);
int settings_begin_restore(void);
void settings_restore_poll(void);
void scope_boot_cursor_poll(void);
void scope_boot_log(const char *message);
void boot_screen_start(void);
void scope_boot_begin(unsigned from_flash);
void scope_lifecycle_poll(void);
unsigned settings_command(const uint8_t *command, unsigned length, uint8_t *reply);
unsigned scope_command(const uint8_t *command, unsigned command_length, uint8_t *reply);
int fpga_read_words(unsigned word_address, void *data, int count);
void display_prepare_from_fel(void);
void display_init_800x480(uintptr_t framebuffer);
void display_present(uintptr_t framebuffer);
int display_present_pending(void);
void display_color_bars(uint16_t *framebuffer);
void display_backlight(int enabled);
int fpga_write_words(unsigned word_address, const void *data, int count);
unsigned usb_high_speed(void);
void usb_debug(unsigned *values);
void usb_transport_state(unsigned *values);
unsigned channel_calibration_command(const uint8_t *command,
                                     unsigned length,
                                     uint8_t *reply,
                                     unsigned channel_index);
#endif
