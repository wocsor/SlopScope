#ifndef SCOPE_STATE_H
#define SCOPE_STATE_H
#include "range_plan.h"
#include "storage.h"
enum capture_phase {
    CAPTURE_IDLE = 0,
    CAPTURE_CONFIGURING = 1,
    CAPTURE_WAITING = 2,
    CAPTURE_TRANSFERRING = 3,
    CAPTURE_COMPLETE = 5,
    CAPTURE_FAILED = 6
};
enum setup_phase { SETUP_IDLE = 0, SETUP_RUNNING = 1, SETUP_COMPLETE = 2, SETUP_FAILED = 3 };
enum fpga_phase {
    FPGA_CONFIG_IDLE = 0,
    FPGA_CONFIG_HOLD_RESET = 1,
    FPGA_CONFIG_WAIT_INIT = 2,
    FPGA_CONFIG_SHIFT_IMAGE = 3,
    FPGA_CONFIG_WAIT_DONE = 4,
    FPGA_CONFIG_READY = 5,
    FPGA_CONFIG_FAILED = 6,
    FPGA_CONFIG_FINISH_RESET = 7
};
enum boot_phase {
    BOOT_IDLE = 0,
    BOOT_LOAD_ASSETS = 1,
    BOOT_CONFIGURE_FPGA = 2,
    BOOT_TEST_ADC = 3,
    BOOT_CONFIGURE_ADC = 4,
    BOOT_CONFIGURE_ANALOG = 5,
    BOOT_RESTORE_SETTINGS = 6,
    BOOT_ACQUIRING = 7,
    BOOT_FAILED = 99
};
struct scope_fpga_state {
    unsigned int state;
    unsigned int bytes_sent;
    unsigned int error;
    unsigned int slow_clock;
    unsigned int spi_ready;
    uint32_t started_ticks;
};
struct scope_display_state {
    unsigned int screen_on;
    unsigned int activity_heartbeat;
    unsigned int view_start;
    unsigned int view_points;
    unsigned int draw_buffer_valid;
    unsigned int display_sequence;
    unsigned int display_frames;
    unsigned int display_redraw_requested;
    unsigned int displayed_buffer_index;
    unsigned int draw_buffer_index;
    unsigned int buffer_ready[2];
    volatile uint16_t *draw_framebuffer;
    uint32_t display_last_ticks;
};
struct scope_boot_state {
    unsigned int console_active;
    unsigned int stage;
    unsigned int error;
};
struct scope_capture_state {
    unsigned int rate_index;
    unsigned int pending_rate_index;
    unsigned int normal_mode;
    unsigned int stream_compression;
    unsigned int roll_live;
    unsigned int capture_roll;
    unsigned int roll_configured;
    uint32_t roll_fill_started;
    unsigned int roll_filling;
    unsigned int roll_fill_ms;
    unsigned int capture_average_epoch;
    unsigned int live_enabled;
    unsigned int live_epoch;
    unsigned int epoch;
    unsigned int continuous_sweep;
    uint32_t setup_started_ms;
    uint32_t roll_fill_started_ms;
    uint32_t roll_transfer_started_ms;
    unsigned int state;
    unsigned int error;
    unsigned int csi_flags;
    unsigned int changed_samples;
    uint32_t started_ticks;
    uint32_t started_ms;
    unsigned int capture_setup_ticks;
    unsigned int capture_wait_ticks;
    unsigned int capture_process_ticks;
};
struct scope_pulse_state {
    unsigned int candidates;
    unsigned int accepted;
    unsigned int rejected;
    unsigned int auto_fallbacks;
    unsigned int native;
    int high_code;
    int low_code;
    unsigned int positive;
};
struct scope_frame_state {
    unsigned int valid_start;
    unsigned int valid_count;
    unsigned int is_roll;
    unsigned int sequence;
    uint32_t words[1016];
    uint32_t legacy_words[1016];
    uint32_t legacy_extra[8];
    unsigned int legacy_ready;
    unsigned int legacy_pulse_sampled;
    unsigned int legacy_alignment;
    unsigned int channel_alignment;
    uint32_t calibration_ids[2];
    uint32_t extra_fields[8];
    unsigned int pulse_sampled;
};
struct scope_configuration_state {
    struct scope_features features;
    struct scope_features active_features;
    struct scope_average averaged;
    unsigned int average_epoch;
    struct scope_acquisition_control acquisition;
    struct scope_acquisition_control active_acquisition;
};
struct scope_trigger_state {
    unsigned int enabled;
    unsigned int active;
    unsigned int forced;
    unsigned int force_requested;
    struct scope_trigger configured;
    struct scope_trigger active_config;
    struct scope_advanced_trigger advanced;
    struct scope_advanced_trigger active_advanced;
};
struct scope_record_state {
    unsigned int active;
    unsigned int channel;
    unsigned int depth;
    unsigned int ready;
    unsigned int sequence;
    unsigned int channel_mask;
    unsigned int first_channel;
    unsigned int filling;
    unsigned int reading;
    unsigned int fill_ms;
    unsigned int forced;
    uint32_t armed_ticks;
    uint32_t completed_ticks;
    uint32_t fill_started_ms;
};
struct scope_channels_state {
    int position_millidivisions[2];
    unsigned int probe_multiplier[2];
    unsigned int visible[2];
    unsigned int inverse[2];
    unsigned int range[2];
    unsigned int coupling[2];
    unsigned int offset_dac[2];
};
struct scope_analog_state {
    int job_position;
    unsigned int state;
    unsigned int step_index;
    unsigned int error;
    unsigned int wait_ticks;
    unsigned int job_kind;
    unsigned int job_channel;
    unsigned int job_range;
    unsigned int job_coupling;
    unsigned int job_offset;
    unsigned int shared_register_shadow;
    unsigned int configured;
    unsigned int job_bandwidth;
    struct analog_range_plan range_job;
};
struct scope_adc_state {
    unsigned int state;
    unsigned int step_index;
    unsigned int wait;
    unsigned int error;
    unsigned int mode;
};
struct scope_settings_state {
    unsigned int restore_state;
    unsigned int save_held;
    unsigned int dirty;
    unsigned int save_error;
    unsigned int save_delay;
    uint32_t changed;
    struct scope_saved_settings saved;
};
struct scope_runtime_state {
    struct scope_fpga_state fpga;
    struct scope_display_state display;
    struct scope_boot_state boot;
    struct scope_capture_state capture;
    struct scope_pulse_state pulse;
    struct scope_frame_state frame;
    struct scope_configuration_state configuration;
    struct scope_trigger_state trigger;
    struct scope_record_state record;
    struct scope_channels_state channels;
    struct scope_analog_state analog;
    struct scope_adc_state adc;
    struct scope_settings_state settings;
};
extern struct scope_runtime_state scope_state;
void scope_initialize(void);
#endif
