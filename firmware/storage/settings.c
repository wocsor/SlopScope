#include "scope_internal.h"

static unsigned int restore_channel;

int scope_saved_settings_valid(const struct scope_saved_settings *saved_settings) {
    if (saved_settings->voltage_baseline > 2 || !scope_advanced_valid(&saved_settings->advanced) ||
        !scope_acquisition_valid(&saved_settings->acquisition) ||
        !scope_features_valid(&saved_settings->features)) {
        return 0;
    }
    if (saved_settings->features.trigger_family && saved_settings->trigger.sweep != 0) {
        return 0;
    }
    if (saved_settings->rate < 1 || saved_settings->rate >= SCOPE_RATE_COUNT ||
        saved_settings->points < 10 || saved_settings->points > 2000 ||
        saved_settings->start > 2000 - saved_settings->points ||
        saved_settings->trigger_enabled > 1 || saved_settings->run > 1) {
        return 0;
    }
    const struct scope_trigger *trigger_config = &saved_settings->trigger;
    if (trigger_config->source > 1 || trigger_config->slope > 1 || trigger_config->sweep > 2 ||
        trigger_config->coupling > 1 || trigger_config->use_code > 1 ||
        trigger_config->code < -127 || trigger_config->code > 126 ||
        trigger_config->level_uv < -1000000000 || trigger_config->level_uv > 1000000000 ||
        trigger_config->holdoff_ns < 100 || trigger_config->holdoff_ns > 1000000000) {
        return 0;
    }
    for (unsigned channel_index = 0; channel_index < 2; channel_index++) {
        if (saved_settings->range[channel_index] >= 14 ||
            saved_settings->coupling[channel_index] > 1 ||
            saved_settings->position[channel_index] < -4000 ||
            saved_settings->position[channel_index] > 4000 ||
            saved_settings->visible[channel_index] > 1 ||
            saved_settings->inverse[channel_index] > 1) {
            return 0;
        }
        if (saved_settings->probe[channel_index] != 1 &&
            saved_settings->probe[channel_index] != 10 &&
            saved_settings->probe[channel_index] != 100 &&
            saved_settings->probe[channel_index] != 1000) {
            return 0;
        }
        if ((saved_settings->range[channel_index] < 2 ||
             saved_settings->range[channel_index] > 11) &&
            saved_settings->position[channel_index]) {
            return 0;
        }
        for (unsigned i = 0; i < 14; i++) {
            if (!scope_adc_gain_valid(&saved_settings->adc_gain[channel_index][i])) {
                return 0;
            }
            const struct scope_voltage_cal *voltage_calibration =
                &saved_settings->cal[channel_index][i];
            if (voltage_calibration->valid > 2) {
                return 0;
            }
            if (voltage_calibration->valid &&
                (voltage_calibration->zero < -(128 << 16) ||
                 voltage_calibration->zero > (127 << 16) || !voltage_calibration->gain ||
                 voltage_calibration->gain > 1024000000u || voltage_calibration->dac > 65535 ||
                 voltage_calibration->slope < -65536 || voltage_calibration->slope > 65536)) {
                return 0;
            }
        }
    }
    return 1;
}

void settings_capture(struct scope_saved_settings *saved_settings) {
    for (unsigned channel_index = 0; channel_index < 2; channel_index++) {
        saved_settings->range[channel_index] = scope_state.channels.range[channel_index];
        saved_settings->coupling[channel_index] = scope_state.channels.coupling[channel_index];
        saved_settings->position[channel_index] =
            scope_state.channels.position_millidivisions[channel_index];
        saved_settings->probe[channel_index] = scope_state.channels.probe_multiplier[channel_index];
        saved_settings->visible[channel_index] = scope_state.channels.visible[channel_index];
        saved_settings->inverse[channel_index] = scope_state.channels.inverse[channel_index];
        for (unsigned i = 0; i < 14; i++) {
            scope_get_reference_voltage_cal(
                channel_index, i, &saved_settings->cal[channel_index][i]);
            scope_adc_gain_get_reference(
                channel_index, i, &saved_settings->adc_gain[channel_index][i]);
        }
    }
    saved_settings->voltage_baseline = scope_voltage_baseline();
    saved_settings->rate = scope_state.capture.pending_rate_index
                               ? scope_state.capture.pending_rate_index
                               : scope_state.capture.rate_index;
    saved_settings->start = scope_state.display.view_start;
    saved_settings->points = scope_state.display.view_points;
    saved_settings->trigger_enabled = scope_state.trigger.enabled;
    saved_settings->run = scope_state.capture.live_enabled;
    saved_settings->trigger = scope_state.trigger.configured;
    saved_settings->advanced = scope_state.trigger.advanced;
    saved_settings->acquisition = scope_state.configuration.acquisition;
    trigger_bank_store();
    saved_settings->features = scope_state.configuration.features;
}

int settings_begin_restore(void) {
    video_control_normalize(&scope_state.settings.saved.trigger,
                            &scope_state.settings.saved.advanced);
    for (unsigned channel_index = 0; channel_index < 3; channel_index++) {
        struct scope_trigger_bank *trigger_bank =
            channel_index < 2 ? &scope_state.settings.saved.features.bank[channel_index]
                              : &scope_state.settings.saved.features.normal;
        video_control_normalize(&trigger_bank->trigger, &trigger_bank->advanced);
    }
    if (scope_state.settings.saved.voltage_baseline != 1) {
        for (unsigned channel_index = 0; channel_index < 2; channel_index++) {
            if (scope_state.settings.saved.range[channel_index] < 3 ||
                scope_state.settings.saved.range[channel_index] > 11) {
                scope_state.settings.saved.range[channel_index] =
                    scope_state.settings.saved.range[channel_index] < 3 ? 3 : 11;
                scope_state.settings.saved.position[channel_index] = 0;
            }
        }
    }
    if (!scope_saved_settings_valid(&scope_state.settings.saved)) {
        return 0;
    }
    scope_voltage_baseline_set(scope_state.settings.saved.voltage_baseline);
    for (unsigned channel_index = 0; channel_index < 2; channel_index++) {
        for (unsigned i = 0; i < 14; i++) {
            if (!scope_set_voltage_cal(
                    channel_index, i, &scope_state.settings.saved.cal[channel_index][i]) ||
                !scope_adc_gain_set(
                    channel_index, i, &scope_state.settings.saved.adc_gain[channel_index][i])) {
                return 0;
            }
        }
    }
    scope_state.capture.live_enabled = 0;
    capture_abort_wait();
    restore_channel = 0;
    scope_state.settings.restore_state = 1;
    return 1;
}

void settings_restore_poll(void) {
    if (scope_state.settings.restore_state != 1 || capture_busy() ||
        scope_state.adc.state == SETUP_RUNNING || scope_state.analog.state == SETUP_RUNNING) {
        return;
    }
    if (scope_state.analog.state == SETUP_FAILED) {
        scope_state.settings.restore_state = 3;
        return;
    }
    if (restore_channel < 2) {
        unsigned channel_index = restore_channel++;
        scope_state.analog.job_bandwidth =
            scope_state.settings.saved.features.bandwidth[channel_index];
        scope_state.analog.job_channel = channel_index;
        scope_state.analog.job_range = scope_state.settings.saved.range[channel_index];
        scope_state.analog.job_coupling = scope_state.settings.saved.coupling[channel_index];
        scope_state.analog.job_position = scope_state.settings.saved.position[channel_index];
        if (!scope_effective_position_dac(channel_index,
                                          scope_state.analog.job_range,
                                          scope_state.analog.job_position,
                                          &scope_state.analog.job_offset)) {
            scope_state.settings.restore_state = 3;
            return;
        }
        scope_state.channels.probe_multiplier[channel_index] =
            scope_state.settings.saved.probe[channel_index];
        scope_state.channels.visible[channel_index] =
            scope_state.settings.saved.visible[channel_index];
        scope_state.channels.inverse[channel_index] =
            scope_state.settings.saved.inverse[channel_index];
        scope_state.analog.job_kind = 1;
        analog_plan_range(channel_index,
                          scope_state.analog.job_range,
                          3,
                          scope_state.analog.shared_register_shadow,
                          &scope_state.analog.range_job);
        scope_state.analog.step_index = scope_state.analog.error = scope_state.analog.wait_ticks =
            0;
        scope_state.analog.state = SETUP_RUNNING;
        return;
    }
    scope_state.capture.rate_index = scope_state.settings.saved.rate;
    scope_state.capture.pending_rate_index = 0;
    scope_state.display.view_points =
        scope_state.settings.saved.points > 1998 ? 1998 : scope_state.settings.saved.points;
    scope_state.display.view_start =
        scope_state.settings.saved.start > 1998 - scope_state.display.view_points
            ? 1998 - scope_state.display.view_points
            : scope_state.settings.saved.start;
    scope_state.trigger.configured = scope_state.settings.saved.trigger;
    scope_state.trigger.advanced = scope_state.settings.saved.advanced;
    scope_state.configuration.acquisition = scope_state.settings.saved.acquisition;
    scope_state.configuration.features = scope_state.settings.saved.features;
    scope_state.configuration.averaged.initialized = 0;
    scope_state.trigger.enabled = scope_state.settings.saved.trigger_enabled;
    scope_state.capture.continuous_sweep =
        scope_state.trigger.configured.sweep == 2 ? 0 : scope_state.trigger.configured.sweep;
    scope_state.display.display_redraw_requested = 1;
    scope_state.settings.restore_state = 2;
    if (scope_state.settings.saved.run) {
        uint8_t reply[CONTROL_REPLY_MAX];
        capture_begin_live(0, reply);
        if (reply[0] == 'E') {
            scope_state.settings.restore_state = 3;
        }
    }
}

unsigned settings_command(const uint8_t *command, unsigned length, uint8_t *reply) {
    if (command_equals(command, length, "VOLTAGE BASELINE")) {
        unsigned reply_length = reply_write_text(reply, "BASELINE mode=");
        reply_length += reply_write_hex32(reply + reply_length, scope_voltage_baseline());
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command, length, "VOLTAGE BASELINE STOCK") ||
        command_equals(command, length, "VOLTAGE BASELINE REFERENCE") ||
        command_equals(command, length, "VOLTAGE BASELINE ZERO")) {
        unsigned mode = command_equals(command, length, "VOLTAGE BASELINE ZERO")
                            ? 2
                            : command_equals(command, length, "VOLTAGE BASELINE REFERENCE");
        if (mode == scope_voltage_baseline()) {
            return reply_write_text(reply, "OK calibration baseline already selected\n");
        }
        if (scope_state.settings.restore_state == 1 || scope_state.analog.state == SETUP_RUNNING ||
            scope_state.adc.state == SETUP_RUNNING || scope_state.record.active ||
            scope_state.settings.save_held) {
            return reply_write_text(reply, "ERR frontend busy\n");
        }
        if (mode != 1 &&
            (scope_state.channels.range[0] < 3 || scope_state.channels.range[0] > 11 ||
             scope_state.channels.range[1] < 3 || scope_state.channels.range[1] > 11)) {
            return reply_write_text(reply, "ERR stock baseline requires supported ranges 3..11\n");
        }
        settings_capture(&scope_state.settings.saved);
        scope_state.settings.saved.voltage_baseline = mode;
        if (!settings_begin_restore()) {
            return reply_write_text(reply, "ERR calibration baseline restore failed\n");
        }
        return reply_write_text(reply, "OK calibration baseline restoring\n");
    }

    if (command_equals(command, length, "CALIBRATION BEGIN")) {
        if (scope_state.settings.save_held) {
            return reply_write_text(reply, "ERR calibration already active\n");
        }
        scope_state.settings.save_held = 1;
        scope_state.settings.dirty = 0;
        return reply_write_text(reply, "OK calibration transaction started\n");
    }
    if (command_equals(command, length, "CALIBRATION END") ||
        command_equals(command, length, "CALIBRATION CANCEL")) {
        if (!scope_state.settings.save_held) {
            return reply_write_text(reply, "ERR no calibration transaction\n");
        }
        scope_state.settings.save_held = 0;
        scope_state.settings.dirty = command_equals(command, length, "CALIBRATION END");
        scope_state.settings.changed = timer_milliseconds();
        scope_state.settings.save_delay = 2000;
        return reply_write_text(reply, "OK calibration transaction closed\n");
    }
    if (command_equals(command, length, "CALIBRATION STATUS")) {
        unsigned reply_length = reply_write_text(reply, "CALIBRATION active=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.settings.save_held);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (length >= 19 && length <= 20 && command_equals(command, 10, "FACTORY CH") &&
        (command[10] == '1' || command[10] == '2') && command_equals(command + 11, 7, " RANGE ")) {
        unsigned range = 0;
        for (unsigned i = 18; i < length; i++) {
            if (command[i] < '0' || command[i] > '9') {
                return reply_write_text(reply, "ERR factory range\n");
            }
            range = range * 10 + command[i] - '0';
        }
        unsigned channel_index = command[10] - '1';
        int zero, slope;
        if (!scope_factory_position(channel_index, range, &zero, &slope)) {
            return reply_write_text(reply, "ERR factory calibration unavailable\n");
        }
        unsigned reply_length = reply_write_text(reply, "FACTORY zero=");
        reply_length += reply_write_hex32(reply + reply_length, (uint32_t)zero);
        reply_length += reply_write_text(reply + reply_length, " slope=");
        reply_length += reply_write_hex32(reply + reply_length, (uint32_t)slope);
        reply_length += reply_write_text(reply + reply_length, " adc=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_factory_adc(channel_index, range, 0));
        reply_length += reply_write_text(reply + reply_length, " trim=");
        reply_length += reply_write_hex32(reply + reply_length,
                                          scope_factory_adc_trim(channel_index, range, 0));
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command, length, "FACTORY STATUS")) {
        unsigned reply_length = reply_write_text(reply, "FACTORY state=");
        reply_length += reply_write_hex32(reply + reply_length, scope_factory_status());
        reply_length += reply_write_text(reply + reply_length, " crc=");
        reply_length += reply_write_hex32(reply + reply_length, scope_factory_crc());
        reply_length += reply_write_text(reply + reply_length, " phase1=");
        reply_length += reply_write_hex32(reply + reply_length, scope_factory_phase(0));
        reply_length += reply_write_text(reply + reply_length, " phase2=");
        reply_length += reply_write_hex32(reply + reply_length, scope_factory_phase(1));
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command, length, "FLASH STATUS")) {
        unsigned reply_length = reply_write_text(reply, "FLASH ready=");
        reply_length += reply_write_hex32(reply + reply_length, scope_flash_init());
        reply_length += reply_write_text(reply + reply_length, " jedec=");
        reply_length += reply_write_hex32(reply + reply_length, scope_flash_id());
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command, length, "BOOT STATUS")) {
        unsigned reply_length = reply_write_text(reply, "BOOT stage=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.boot.stage);
        reply_length += reply_write_text(reply + reply_length, " error=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.boot.error);
        reply_length += reply_write_text(reply + reply_length, " restore=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.settings.restore_state);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command, length, "SETTINGS STATUS")) {
        unsigned reply_length = reply_write_text(reply, "SETTINGS state=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.settings.restore_state);
        reply_length += reply_write_text(reply + reply_length, " sequence=");
        reply_length += reply_write_hex32(reply + reply_length, scope_settings_sequence());
        reply_length += reply_write_text(reply + reply_length, " dirty=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.settings.dirty);
        reply_length += reply_write_text(reply + reply_length, " save_error=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.settings.save_error);
        reply_length += reply_write_text(reply + reply_length, " volatile=");
        reply_length += reply_write_hex32(reply + reply_length, SCOPE_VOLATILE_SETTINGS);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command, length, "SETTINGS SAVE") ||
        command_equals(command, length, "SETTINGS SAVE RUN")) {
        if (SCOPE_VOLATILE_SETTINGS) {
            return reply_write_text(reply, "ERR temporary session cannot save settings\n");
        }
        if (scope_state.settings.save_held || !scope_state.analog.configured ||
            scope_state.analog.state == SETUP_RUNNING || scope_state.adc.state == SETUP_RUNNING ||
            capture_busy() || scope_state.settings.restore_state == 1) {
            return reply_write_text(reply, "ERR stop acquisition before saving\n");
        }
        settings_capture(&scope_state.settings.saved);
        if (command_equals(command, length, "SETTINGS SAVE RUN")) {
            scope_state.settings.saved.run = 1;
        }
        if (!scope_saved_settings_valid(&scope_state.settings.saved) ||
            !scope_settings_save(&scope_state.settings.saved)) {
            return reply_write_text(reply, "ERR settings save failed\n");
        }
        scope_state.settings.dirty = scope_state.settings.save_error = 0;
        return reply_write_text(reply, "OK settings saved\n");
    }
    if (command_equals(command, length, "SETTINGS LOAD")) {
        if (!scope_state.analog.configured || scope_state.capture.live_enabled ||
            scope_state.analog.state == SETUP_RUNNING || scope_state.adc.state == SETUP_RUNNING ||
            capture_busy() || scope_state.settings.restore_state == 1) {
            return reply_write_text(reply, "ERR stop acquisition before loading\n");
        }
        if (scope_settings_load(&scope_state.settings.saved) != 1 || !settings_begin_restore()) {
            return reply_write_text(reply, "ERR no valid saved settings\n");
        }
        return reply_write_text(reply, "OK settings restoring\n");
    }
    return 0;
}
