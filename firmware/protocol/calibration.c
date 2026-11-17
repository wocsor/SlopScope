#include "scope_internal.h"

unsigned channel_calibration_command(const uint8_t *command,
                                     unsigned length,
                                     uint8_t *reply,
                                     unsigned channel_index) {
    if ((length > 15 && command_equals(command + 4, 11, "CAL LEGACY ")) ||
        (length > 16 && command_equals(command + 4, 12, "CAL FACTORY "))) {
        unsigned factory = command[8] == 'F', start = factory ? 16 : 15, range;
        struct scope_adc_gain gain_settings;
        if (!command_parse_unsigned(command + start, length - start, &range) || range >= 14) {
            return reply_write_text(reply, "ERR calibration range\n");
        }
        if (factory) {
            if (!scope_adc_gain_factory(channel_index, range, &gain_settings)) {
                return reply_write_text(reply, "ERR factory gain unavailable\n");
            }
        } else {
            scope_adc_gain_legacy(channel_index, range, &gain_settings);
        }
        unsigned reply_length = reply_write_text(reply, "ADC scale=");
        reply_length += reply_write_hex32(reply + reply_length, gain_settings.scale);
        reply_length += reply_write_text(reply + reply_length, " trim=");
        reply_length += reply_write_hex32(reply + reply_length, gain_settings.trim);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    unsigned reference_query = length > 18 && command_equals(command + 4, 14, "CAL REFERENCE ");
    if ((length > 12 && command_equals(command + 4, 8, "CAL GET ")) || reference_query) {
        unsigned start = reference_query ? 18 : 12;
        unsigned range;
        if (!command_parse_unsigned(command + start, length - start, &range) || range >= 14) {
            return reply_write_text(reply, "ERR calibration range\n");
        }
        struct scope_voltage_cal voltage_calibration;
        struct scope_adc_gain gain_settings;
        if (reference_query) {
            scope_get_reference_voltage_cal(channel_index, range, &voltage_calibration);
            scope_adc_gain_get_reference(channel_index, range, &gain_settings);
        } else {
            scope_get_voltage_cal(channel_index, range, &voltage_calibration);
            scope_adc_gain_get(channel_index, range, &gain_settings);
        }
        unsigned reply_length = reply_write_text(reply, "CAL range=");
        reply_length += reply_write_hex32(reply + reply_length, range);
        reply_length += reply_write_text(reply + reply_length, " zero=");
        reply_length += reply_write_hex32(reply + reply_length, voltage_calibration.zero);
        reply_length += reply_write_text(reply + reply_length, " gain=");
        reply_length += reply_write_hex32(reply + reply_length, voltage_calibration.gain);
        reply_length += reply_write_text(reply + reply_length, " dac=");
        reply_length += reply_write_hex32(reply + reply_length, voltage_calibration.dac);
        reply_length += reply_write_text(reply + reply_length, " slope=");
        reply_length += reply_write_hex32(reply + reply_length, voltage_calibration.slope);
        reply_length += reply_write_text(reply + reply_length, " valid=");
        reply_length += reply_write_hex32(reply + reply_length, voltage_calibration.valid);
        reply_length += reply_write_text(reply + reply_length, " adc_scale=");
        reply_length += reply_write_hex32(reply + reply_length, gain_settings.scale);
        reply_length += reply_write_text(reply + reply_length, " adc_trim=");
        reply_length += reply_write_hex32(reply + reply_length, gain_settings.trim);
        if (!reference_query) {
            reply_length += reply_write_text(reply + reply_length, " calibration_id=");
            reply_length += reply_write_hex32(reply + reply_length,
                                              scope_adc_calibration_id(channel_index, range));
        }
        reply[reply_length++] = '\n';
        return reply_length;
    }
    unsigned migrate_one = length > 16 && command_equals(command + 4, 12, "CAL MIGRATE ");
    unsigned migrate_two = length > 17 && command_equals(command + 4, 13, "CAL MIGRATE2 ");
    if (migrate_one || migrate_two) {
        if (scope_voltage_baseline() != 1) {
            return reply_write_text(reply,
                                    "ERR select REFERENCE baseline before calibration update\n");
        }
        if (scope_state.capture.live_enabled || capture_busy() || scope_state.record.active ||
            scope_state.analog.state == SETUP_RUNNING || scope_state.adc.state == SETUP_RUNNING) {
            return reply_write_text(reply, "ERR stop acquisition before calibration update\n");
        }
        int32_t values[11];
        unsigned fields = migrate_two ? 11 : 5, count = 0, start = migrate_two ? 17 : 16;
        for (unsigned i = start; i <= length; i++) {
            if (i == length || command[i] == ' ') {
                if (count >= fields ||
                    !command_parse_signed(command + start, i - start, &values[count++])) {
                    return reply_write_text(reply, "ERR migration argument\n");
                }
                start = i + 1;
            }
        }
        if (count != fields || values[0] < 0 || values[0] >= 14) {
            return reply_write_text(reply, "ERR migration argument\n");
        }
        struct scope_voltage_cal voltage_calibration;
        struct scope_adc_gain gain_settings, legacy, factory;
        unsigned range = (unsigned)values[0];
        scope_get_voltage_cal(channel_index, range, &voltage_calibration);
        scope_adc_gain_get(channel_index, range, &gain_settings);
        scope_adc_gain_legacy(channel_index, range, &legacy);
        if (!scope_voltage_cal_valid(&voltage_calibration) || gain_settings.scale != legacy.scale ||
            gain_settings.trim != legacy.trim) {
            return reply_write_text(reply, "ERR migration requires valid legacy calibration\n");
        }
        if (!scope_adc_gain_factory(channel_index, range, &factory)) {
            return reply_write_text(reply, "ERR factory gain unavailable\n");
        }
        struct scope_adc_gain_transfer transfer[2];
        unsigned points = migrate_two ? 2 : 1;
        if (migrate_one) {
            transfer[0] = (struct scope_adc_gain_transfer){
                voltage_calibration.dac, values[1], values[2], values[3], values[4]};
        } else {
            for (unsigned i = 0; i < 2; i++) {
                unsigned j = i ? 6 : 1;
                if (values[j] < 0 || values[j] > 65535) {
                    return reply_write_text(reply, "ERR migration DAC argument\n");
                }
                transfer[i] = (struct scope_adc_gain_transfer){(unsigned)values[j],
                                                               values[j + 1],
                                                               values[j + 2],
                                                               values[j + 3],
                                                               values[j + 4]};
            }
        }
        struct scope_voltage_cal migrated;
        if (!scope_adc_gain_migrate_calibration(
                &voltage_calibration, transfer, points, &migrated)) {
            return reply_write_text(reply,
                                    migrate_two
                                        ? "ERR two-position gain map invalid or inconsistent\n"
                                        : "ERR measured gain ratio outside safe bounds\n");
        }
        scope_set_voltage_cal(channel_index, range, &migrated);
        scope_adc_gain_set(channel_index, range, &factory);
        unsigned changed =
            factory.scale != gain_settings.scale || factory.trim != gain_settings.trim;
        if (changed && scope_state.analog.configured &&
            scope_state.channels.range[channel_index] == range) {
            unsigned offset_dac;
            if (!scope_effective_position_dac(
                    channel_index,
                    range,
                    scope_state.channels.position_millidivisions[channel_index],
                    &offset_dac)) {
                scope_set_voltage_cal(channel_index, range, &voltage_calibration);
                scope_adc_gain_set(channel_index, range, &gain_settings);
                return reply_write_text(reply, "ERR migrated position outside DAC range\n");
            }
            scope_state.analog.job_channel = channel_index;
            scope_state.analog.job_range = range;
            scope_state.analog.job_coupling = scope_state.channels.coupling[channel_index];
            scope_state.analog.job_position =
                scope_state.channels.position_millidivisions[channel_index];
            scope_state.analog.job_offset = offset_dac;
            scope_state.analog.job_bandwidth =
                scope_state.configuration.features.bandwidth[channel_index];
            analog_plan_range(channel_index,
                              range,
                              3,
                              scope_state.analog.shared_register_shadow,
                              &scope_state.analog.range_job);
            scope_state.analog.job_kind = 1;
            scope_state.analog.step_index = scope_state.analog.error =
                scope_state.analog.wait_ticks = 0;
            scope_state.analog.state = SETUP_RUNNING;
        }
        if (scope_state.display.screen_on && scope_state.frame.sequence) {
            scope_state.display.display_redraw_requested = 1;
        }
        return reply_write_text(
            reply,
            migrate_two
                ? "OK factory gain and position response migrated; verify volts before saving\n"
                : "OK factory gain migrated from one measured position; verify volts before "
                  "saving\n");
    }
    if ((length > 12 && command_equals(command + 4, 8, "CAL SET ")) ||
        (length > 13 && command_equals(command + 4, 9, "CAL SET2 "))) {
        if (scope_state.capture.live_enabled || capture_busy() || scope_state.record.active ||
            scope_state.analog.state == SETUP_RUNNING || scope_state.adc.state == SETUP_RUNNING) {
            return reply_write_text(reply, "ERR stop acquisition before calibration update\n");
        }
        if (scope_voltage_baseline() != 1) {
            return reply_write_text(reply,
                                    "ERR select REFERENCE baseline before calibration update\n");
        }
        unsigned paired = command[11] == '2', fields = paired ? 8 : 6, start = paired ? 13 : 12,
                 count = 0;
        int32_t values[8];
        for (unsigned i = start; i <= length; i++) {
            if (i == length || command[i] == ' ') {
                if (count >= fields ||
                    !command_parse_signed(command + start, i - start, &values[count++])) {
                    return reply_write_text(reply, "ERR calibration argument\n");
                }
                start = i + 1;
            }
        }
        if (count != fields || values[0] < 0 || values[0] >= 14) {
            return reply_write_text(reply, "ERR calibration argument\n");
        }
        unsigned range = (unsigned)values[0];
        struct scope_voltage_cal voltage_calibration = {values[1],
                                                        (unsigned)values[2],
                                                        (unsigned)values[3],
                                                        values[4],
                                                        (unsigned)values[5]},
                                 previous_calibration;
        struct scope_adc_gain gain_settings, previous;
        if (paired) {
            gain_settings = (struct scope_adc_gain){(unsigned)values[6], (unsigned)values[7]};
        } else {
            scope_adc_gain_legacy(channel_index, range, &gain_settings);
        }
        if (!scope_voltage_cal_valid(&voltage_calibration) ||
            !scope_adc_gain_valid(&gain_settings)) {
            return reply_write_text(reply, "ERR calibration argument\n");
        }
        scope_get_voltage_cal(channel_index, range, &previous_calibration);
        scope_adc_gain_get(channel_index, range, &previous);
        scope_set_voltage_cal(channel_index, range, &voltage_calibration);
        scope_adc_gain_set(channel_index, range, &gain_settings);
        unsigned changed =
            gain_settings.scale != previous.scale || gain_settings.trim != previous.trim;
        if (changed && scope_state.analog.configured &&
            scope_state.channels.range[channel_index] == range) {
            unsigned offset_dac;
            if (!scope_effective_position_dac(
                    channel_index,
                    range,
                    scope_state.channels.position_millidivisions[channel_index],
                    &offset_dac)) {
                scope_set_voltage_cal(channel_index, range, &previous_calibration);
                scope_adc_gain_set(channel_index, range, &previous);
                return reply_write_text(reply, "ERR calibrated position outside DAC range\n");
            }
            scope_state.analog.job_channel = channel_index;
            scope_state.analog.job_range = range;
            scope_state.analog.job_coupling = scope_state.channels.coupling[channel_index];
            scope_state.analog.job_position =
                scope_state.channels.position_millidivisions[channel_index];
            scope_state.analog.job_offset = offset_dac;
            scope_state.analog.job_bandwidth =
                scope_state.configuration.features.bandwidth[channel_index];
            analog_plan_range(channel_index,
                              range,
                              3,
                              scope_state.analog.shared_register_shadow,
                              &scope_state.analog.range_job);
            scope_state.analog.job_kind = 1;
            scope_state.analog.step_index = scope_state.analog.error =
                scope_state.analog.wait_ticks = 0;
            scope_state.analog.state = SETUP_RUNNING;
        }
        if (scope_state.display.screen_on && scope_state.frame.sequence) {
            scope_state.display.display_redraw_requested = 1;
        }
        return reply_write_text(reply, "OK paired reference calibration set\n");
    }
    return 0;
}
