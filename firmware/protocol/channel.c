#include "scope_internal.h"

unsigned channel_command(const uint8_t *command, unsigned length, uint8_t *reply) {
    if (length < 5 || command[0] != 'C' || command[1] != 'H' ||
        (command[2] != '1' && command[2] != '2') || command[3] != ' ') {
        return 0;
    }
    unsigned channel_index = command[2] - '1';
    if (command_equals(command + 4, length - 4, "BANDWIDTH")) {
        unsigned reply_length = reply_write_text(reply, "BANDWIDTH enabled=");
        reply_length += reply_write_hex32(
            reply + reply_length, scope_state.configuration.features.bandwidth[channel_index]);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command + 4, length - 4, "STATUS")) {
        unsigned reply_length = reply_write_text(reply, "CH range=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.channels.range[channel_index]);
        reply_length += reply_write_text(reply + reply_length, " coupling=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.channels.coupling[channel_index]);
        reply_length += reply_write_text(reply + reply_length, " offset_dac=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.channels.offset_dac[channel_index]);
        reply_length += reply_write_text(reply + reply_length, " valid=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.analog.configured);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command + 4, length - 4, "VIEW")) {
        unsigned reply_length = reply_write_text(reply, "CH position_mdiv=");
        reply_length += reply_write_hex32(
            reply + reply_length,
            (uint32_t)scope_state.channels.position_millidivisions[channel_index]);
        reply_length += reply_write_text(reply + reply_length, " probe=");
        reply_length += reply_write_hex32(reply + reply_length,
                                          scope_state.channels.probe_multiplier[channel_index]);
        reply_length += reply_write_text(reply + reply_length, " visible=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.channels.visible[channel_index]);
        reply_length += reply_write_text(reply + reply_length, " inverse=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.channels.inverse[channel_index]);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    unsigned calibration_reply = channel_calibration_command(command, length, reply, channel_index);
    if (calibration_reply) {
        return calibration_reply;
    }
    unsigned parsed_value;
    if ((command_equals(command + 4, length - 4, "DISPLAY ON") ||
         command_equals(command + 4, length - 4, "DISPLAY OFF")) ||
        (command_equals(command + 4, length - 4, "INVERSE ON") ||
         command_equals(command + 4, length - 4, "INVERSE OFF"))) {
        if (command[4] == 'D') {
            scope_state.channels.visible[channel_index] = command[length - 1] == 'N';
        } else {
            scope_state.channels.inverse[channel_index] = command[length - 1] == 'N';
        }
        if (scope_state.display.screen_on && scope_state.frame.sequence) {
            scope_state.display.display_redraw_requested = 1;
        }
        return reply_write_text(reply, "OK channel display set\n");
    }
    if (length > 10 && command_equals(command + 4, 6, "PROBE ") &&
        command_parse_unsigned(command + 10, length - 10, &parsed_value) &&
        (parsed_value == 1 || parsed_value == 10 || parsed_value == 100 || parsed_value == 1000)) {
        scope_state.channels.probe_multiplier[channel_index] = parsed_value;
        capture_abort_wait();
        if (scope_state.display.screen_on && scope_state.display.draw_buffer_valid) {
            scope_state.display.display_redraw_requested = 1;
        }
        return reply_write_text(reply, "OK probe factor set\n");
    }

    if (!scope_state.analog.configured) {
        return reply_write_text(reply, "ERR initialize analog first\n");
    }
    if (scope_state.analog.state == SETUP_RUNNING || scope_state.adc.state == SETUP_RUNNING ||
        (capture_busy() && !scope_state.capture.live_enabled)) {
        return reply_write_text(reply, "ERR frontend busy\n");
    }
    scope_state.analog.job_bandwidth = scope_state.configuration.features.bandwidth[channel_index];
    scope_state.analog.job_channel = channel_index;
    scope_state.analog.job_range = scope_state.channels.range[channel_index];
    scope_state.analog.job_coupling = scope_state.channels.coupling[channel_index];
    scope_state.analog.job_offset = scope_state.channels.offset_dac[channel_index];
    scope_state.analog.job_position = scope_state.channels.position_millidivisions[channel_index];
    int32_t position_value;
    if (length > 10 && command_equals(command + 4, 6, "RANGE ") &&
        command_parse_unsigned(command + 10, length - 10, &parsed_value) && parsed_value < 14) {
        if (scope_voltage_baseline() != 1 && (parsed_value < 3 || parsed_value > 11)) {
            return reply_write_text(reply, "ERR stock baseline supports ranges 3..11\n");
        }
        scope_state.analog.job_range = parsed_value;
        unsigned test_dac;
        if (!scope_effective_position_dac(
                channel_index, parsed_value, scope_state.analog.job_position, &test_dac)) {
            return reply_write_text(reply, "ERR position unavailable in range\n");
        }
        scope_state.analog.job_kind = 1;
        analog_plan_range(channel_index,
                          parsed_value,
                          3,
                          scope_state.analog.shared_register_shadow,
                          &scope_state.analog.range_job);
    } else if (command_equals(command + 4, length - 4, "BANDWIDTH ON") ||
               command_equals(command + 4, length - 4, "BANDWIDTH OFF")) {
        scope_state.analog.job_bandwidth = command[length - 1] == 'N';
        scope_state.analog.job_kind = 4;
        analog_plan_range(channel_index,
                          scope_state.analog.job_range,
                          3,
                          scope_state.analog.shared_register_shadow,
                          &scope_state.analog.range_job);
    } else if (command_equals(command + 4, length - 4, "COUPLING DC") ||
               command_equals(command + 4, length - 4, "COUPLING AC")) {
        scope_state.analog.job_coupling = command[length - 2] == 'A';
        scope_state.analog.job_kind = 2;
    } else if (length > 13 && command_equals(command + 4, 9, "POSITION ") &&
               command_parse_signed(command + 13, length - 13, &position_value) &&
               scope_effective_position_dac(
                   channel_index, scope_state.analog.job_range, position_value, &parsed_value)) {
        scope_state.analog.job_position = position_value;
        scope_state.analog.job_offset = parsed_value;
        scope_state.analog.job_kind = 3;
    } else if (length > 11 && command_equals(command + 4, 7, "OFFSET ") &&
               command_parse_unsigned(command + 11, length - 11, &parsed_value) &&
               parsed_value <= 65535) {
        scope_state.analog.job_offset = parsed_value;
        scope_state.analog.job_position = 0;
        scope_state.analog.job_kind = 3;
    } else {
        return reply_write_text(reply, "ERR channel argument\n");
    }
    capture_abort_wait();
    scope_state.analog.step_index = scope_state.analog.error = scope_state.analog.wait_ticks = 0;
    scope_state.analog.state = SETUP_RUNNING;
    return reply_write_text(reply, "OK channel update started\n");
}
