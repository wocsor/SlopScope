#include "scope_internal.h"

static unsigned int boot_assets_from_flash;

static int boot_issue(const char *command);

void scope_boot_begin(unsigned from_flash) {
    boot_assets_from_flash = from_flash;
    scope_state.boot.stage = BOOT_LOAD_ASSETS;
    scope_state.boot.error = 0;
}

static int boot_issue(const char *command) {
    uint8_t reply[CONTROL_REPLY_MAX];
    unsigned command_length = 0;
    while (command[command_length]) {
        command_length++;
    }
    scope_command((const uint8_t *)command, command_length, reply);
    if (reply[0] == 'E') {
        scope_state.boot.error = 3;
        scope_state.boot.stage = BOOT_FAILED;
        return 0;
    }
    return 1;
}

void scope_lifecycle_poll(void) {
    scope_boot_cursor_poll();
    settings_restore_poll();
    if (scope_state.settings.dirty && scope_settings_write_blocked()) {
        scope_state.settings.save_error = 1;
    }
    if (!SCOPE_VOLATILE_SETTINGS && !scope_settings_write_blocked() &&
        scope_state.boot.stage == BOOT_ACQUIRING && !scope_state.settings.save_held &&
        !scope_state.record.active && scope_state.settings.dirty &&
        (timer_elapsed_ms(scope_state.settings.changed) >= scope_state.settings.save_delay) &&
        scope_state.analog.state != SETUP_RUNNING && scope_state.adc.state != SETUP_RUNNING &&
        scope_state.settings.restore_state == 2) {
        capture_abort_wait();
    }
    if (!SCOPE_VOLATILE_SETTINGS && !scope_settings_write_blocked() &&
        scope_state.boot.stage == BOOT_ACQUIRING && !scope_state.settings.save_held &&
        !scope_state.record.active && scope_state.settings.dirty &&
        (timer_elapsed_ms(scope_state.settings.changed) >= scope_state.settings.save_delay) &&
        !capture_busy() && scope_state.analog.state != SETUP_RUNNING &&
        scope_state.adc.state != SETUP_RUNNING && scope_state.settings.restore_state == 2) {
        settings_capture(&scope_state.settings.saved);
        if (scope_saved_settings_valid(&scope_state.settings.saved) &&
            scope_settings_save(&scope_state.settings.saved)) {
            scope_state.settings.dirty = scope_state.settings.save_error = 0;
            scope_state.settings.save_delay = 2000;
        } else {
            scope_state.settings.save_error = 1;
            scope_state.settings.changed = timer_milliseconds();
            scope_state.settings.save_delay = 10000;
        }
    }
    if (scope_state.boot.stage == BOOT_LOAD_ASSETS) {
        boot_screen_start();
        if (boot_assets_from_flash && !scope_assets_from_flash()) {
            scope_state.boot.error = 1;
            scope_state.boot.stage = BOOT_FAILED;
            return;
        }
        scope_boot_log("READING FACTORY CALIBRATION");
        scope_factory_load();
        scope_boot_log(scope_factory_status() <= 2 ? "FACTORY CALIBRATION LOADED"
                                                   : "FACTORY CALIBRATION UNAVAILABLE");
        scope_boot_log("CONFIGURING FPGA");
        if (boot_issue("FRONTEND LOAD")) {
            scope_state.boot.stage = BOOT_CONFIGURE_FPGA;
        }
    } else if (scope_state.boot.stage == BOOT_CONFIGURE_FPGA) {
        if (scope_state.fpga.state == FPGA_CONFIG_FAILED) {
            scope_state.boot.error = 2;
            scope_state.boot.stage = BOOT_FAILED;
        } else if (scope_state.fpga.state == FPGA_CONFIG_READY) {
            scope_boot_log("INITIALIZING ADC");
            if (boot_issue("SCREEN ON") && boot_issue("ADC TESTINIT")) {
                scope_state.boot.stage = BOOT_TEST_ADC;
            }
        }
    } else if (scope_state.boot.stage == BOOT_TEST_ADC) {
        if (scope_state.adc.state == SETUP_FAILED) {
            scope_state.boot.error = 3;
            scope_state.boot.stage = BOOT_FAILED;
        } else if (scope_state.adc.state == SETUP_COMPLETE) {
            scope_boot_log("SELECTING ACQUISITION MODE");
            if (boot_issue("ADC NORMAL")) {
                scope_state.boot.stage = BOOT_CONFIGURE_ADC;
            }
        }
    } else if (scope_state.boot.stage == BOOT_CONFIGURE_ADC) {
        if (scope_state.adc.state == SETUP_FAILED) {
            scope_state.boot.error = 4;
            scope_state.boot.stage = BOOT_FAILED;
        } else if (scope_state.adc.state == SETUP_COMPLETE) {
            scope_boot_log("INITIALIZING ANALOG FRONTEND");
            if (boot_issue("ANALOG INIT")) {
                scope_state.boot.stage = BOOT_CONFIGURE_ANALOG;
            }
        }
    } else if (scope_state.boot.stage == BOOT_CONFIGURE_ANALOG) {
        if (scope_state.analog.state == SETUP_FAILED) {
            scope_state.boot.error = 5;
            scope_state.boot.stage = BOOT_FAILED;
        } else if (scope_state.analog.state == SETUP_COMPLETE) {
            scope_boot_log("RESTORING SAVED SETUP");
            int loaded = scope_settings_load(&scope_state.settings.saved);
            if (loaded < 0) {
                scope_state.boot.error = 6;
                scope_state.boot.stage = BOOT_FAILED;
                return;
            }
            if (!loaded) {
                settings_capture(&scope_state.settings.saved);
                scope_advanced_defaults(&scope_state.settings.saved.advanced);
                scope_acquisition_defaults(&scope_state.settings.saved.acquisition);
                scope_features_defaults(&scope_state.settings.saved.features);
                scope_state.settings.saved.rate = 10;
                scope_state.settings.saved.points = 750;
                scope_state.settings.saved.start = 0;
                scope_state.settings.saved.trigger_enabled = 1;
                scope_state.settings.saved.run = 1;
                scope_state.settings.saved.trigger =
                    (struct scope_trigger){0, 0, 0, 0, 0, 100, 1, 0};
            }
            if (!settings_begin_restore()) {
                scope_state.boot.error = 7;
                scope_state.boot.stage = BOOT_FAILED;
            } else {
                scope_state.boot.stage = BOOT_RESTORE_SETTINGS;
            }
        }
    } else if (scope_state.boot.stage == BOOT_RESTORE_SETTINGS) {
        if (scope_state.settings.restore_state == 2) {
            scope_boot_log("STARTING LIVE ACQUISITION");
            if (scope_settings_need_migration() && !SCOPE_VOLATILE_SETTINGS) {
                scope_state.settings.dirty = 1;
                scope_state.settings.changed = timer_milliseconds();
            }
            if (scope_state.frame.sequence) {
                scope_state.boot.console_active = 0;
            }
            scope_state.boot.stage = BOOT_ACQUIRING;
        } else if (scope_state.settings.restore_state == 3) {
            scope_state.boot.error = 8;
            scope_state.boot.stage = BOOT_FAILED;
        }
    }
}
