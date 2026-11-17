#include "scope_internal.h"

unsigned capture_command(const uint8_t *command, unsigned length, uint8_t *reply) {
    if (command_equals(command, length, "ACQUIRE STATUS")) {
        unsigned reply_length = reply_write_text(reply, "ACQUIRE mode=");
        reply_length += reply_write_hex32(reply + reply_length,
                                          scope_state.configuration.features.acquire_mode);
        reply_length += reply_write_text(reply + reply_length, " averages=");
        reply_length += reply_write_hex32(reply + reply_length,
                                          scope_state.configuration.features.average_count);
        reply_length += reply_write_text(reply + reply_length, " peak=");
        reply_length += reply_write_hex32(reply + reply_length,
                                          scope_state.configuration.features.acquire_mode == 1);
        reply_length += reply_write_text(reply + reply_length, " frame=");
        reply_length += reply_write_hex32(reply + reply_length, 7);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command, length, "ACQUIRE MODE SAMPLE") ||
        command_equals(command, length, "ACQUIRE MODE PEAK") ||
        command_equals(command, length, "ACQUIRE MODE AVERAGE")) {
        if (command_equals(command, length, "ACQUIRE MODE AVERAGE") &&
            scope_state.capture.rate_index >= 16 && scope_state.capture.roll_live &&
            scope_state.trigger.configured.sweep != 2 &&
            !scope_state.configuration.features.trigger_family &&
            !scope_state.configuration.acquisition.horizontal_enabled) {
            return reply_write_text(reply, "ERR roll acquisition supports SAMPLE or PEAK\n");
        }
        scope_state.configuration.features.acquire_mode =
            command_equals(command, length, "ACQUIRE MODE PEAK")      ? 1
            : command_equals(command, length, "ACQUIRE MODE AVERAGE") ? 2
                                                                      : 0;
        capture_abort_wait();
        scope_state.display.display_redraw_requested = 1;
        return reply_write_text(reply, "OK device acquisition mode set\n");
    }
    if (command_equals(command, length, "ACQUIRE AVERAGES 4") ||
        command_equals(command, length, "ACQUIRE AVERAGES 16") ||
        command_equals(command, length, "ACQUIRE AVERAGES 64") ||
        command_equals(command, length, "ACQUIRE AVERAGES 128")) {
        scope_state.configuration.features.average_count =
            command_equals(command, length, "ACQUIRE AVERAGES 128")  ? 128
            : command_equals(command, length, "ACQUIRE AVERAGES 64") ? 64
            : command_equals(command, length, "ACQUIRE AVERAGES 16") ? 16
                                                                     : 4;
        return reply_write_text(reply, "OK device averaging set\n");
    }

    if (command_equals(command, length, "CAPTURE DECIMATION")) {
        unsigned reply_length = reply_write_text(reply, "DECIMATION override=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.capture.stream_compression);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (length >= 20 && length <= 21 && command_equals(command, 19, "CAPTURE DECIMATION ")) {
        unsigned value = 0;
        for (unsigned i = 19; i < length; i++) {
            if (command[i] < '0' || command[i] > '9') {
                return reply_write_text(reply, "ERR decimation\n");
            }
            value = value * 10 + command[i] - '0';
        }
        if (value != 0 && value != 1 && value != 2 && value != 5 && value != 10) {
            return reply_write_text(reply, "ERR decimation\n");
        }
        if (capture_busy() || scope_state.capture.live_enabled) {
            return reply_write_text(reply, "ERR stop capture before changing decimation\n");
        }
        scope_state.capture.stream_compression = value;
        return reply_write_text(reply, "OK capture decimation set\n");
    }
    if (command_equals(command, length, "CAPTURE MODE")) {
        return reply_write_text(
            reply, scope_state.capture.normal_mode ? "MODE normal\n" : "MODE diagnostic\n");
    }
    if (command_equals(command, length, "CAPTURE MODE NORMAL") ||
        command_equals(command, length, "CAPTURE MODE DIAGNOSTIC")) {
        if (capture_busy() || scope_state.capture.live_enabled) {
            return reply_write_text(reply, "ERR stop capture before changing mode\n");
        }
        scope_state.capture.normal_mode = command_equals(command, length, "CAPTURE MODE NORMAL");
        return reply_write_text(reply, "OK capture mode set\n");
    }
    if (command_equals(command, length, "DISPLAY STATUS")) {
        unsigned reply_length = reply_write_text(reply, "DISPLAY frames=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.display.display_frames);
        reply_length += reply_write_text(reply + reply_length, " sequence=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.display.display_sequence);
        reply_length += reply_write_text(reply + reply_length, " draw_ticks=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.display.display_last_ticks);
        reply_length += reply_write_text(reply + reply_length, " buffer=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.display.displayed_buffer_index);
        reply_length += reply_write_text(reply + reply_length, " pending=");
        reply_length += reply_write_hex32(reply + reply_length, display_present_pending());
        reply_length += reply_write_text(reply + reply_length, " max_fps=30\n");
        return reply_length;
    }
    if (command_equals(command, length, "CAPTURE TIMING")) {
        unsigned reply_length = reply_write_text(reply, "TICKS setup=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.capture.capture_setup_ticks);
        reply_length += reply_write_text(reply + reply_length, " wait=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.capture.capture_wait_ticks);
        reply_length += reply_write_text(reply + reply_length, " process=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.capture.capture_process_ticks);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command, length, "ROLL MODE LIVE") ||
        command_equals(command, length, "ROLL MODE TRIGGERED")) {
        scope_state.capture.roll_live = command_equals(command, length, "ROLL MODE LIVE");
        capture_abort_wait();
        scope_state.capture.roll_configured = 0;
        scope_state.display.display_redraw_requested = 1;
        return reply_write_text(reply, "OK slow acquisition mode set\n");
    }
    if (command_equals(command, length, "ROLL STATUS")) {
        unsigned reply_length = reply_write_text(reply, "ROLL mode=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.capture.roll_live);
        reply_length += reply_write_text(reply + reply_length, " active=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.capture.capture_roll);
        reply_length += reply_write_text(reply + reply_length, " configured=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.capture.roll_configured);
        reply_length += reply_write_text(reply + reply_length, " filling=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.capture.roll_filling);
        unsigned elapsed_ms = (scope_state.capture.roll_fill_started - timer_ticks()) / 24000u;
        reply_length += reply_write_text(reply + reply_length, " remaining_ms=");
        reply_length += reply_write_hex32(reply + reply_length,
                                          scope_state.capture.roll_filling &&
                                                  elapsed_ms < scope_state.capture.roll_fill_ms
                                              ? scope_state.capture.roll_fill_ms - elapsed_ms
                                              : 0);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command, length, "CAPTURE CONFIG")) {
        unsigned reply_length = reply_write_text(reply, "CAP rate_index=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.capture.rate_index);
        reply_length += reply_write_text(reply + reply_length, " nominal_hz=");
        reply_length += reply_write_hex32(reply + reply_length,
                                          sample_rate_numerator(scope_state.capture.rate_index));
        reply_length += reply_write_text(reply + reply_length, " rate_denominator=");
        reply_length += reply_write_hex32(reply + reply_length,
                                          sample_rate_denominator(scope_state.capture.rate_index));
        reply_length +=
            reply_write_text(reply + reply_length,
                             scope_state.capture.normal_mode ? " mode=normal" : " mode=diagnostic");
        reply_length += reply_write_text(reply + reply_length, " decimation=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.capture.stream_compression);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if ((length == 14 || length == 15) && command_equals(command, 13, "CAPTURE RATE ")) {
        unsigned index = 0;
        for (unsigned i = 13; i < length; i++) {
            if (command[i] < '0' || command[i] > '9') {
                return reply_write_text(reply, "ERR rate index\n");
            }
            index = index * 10 + command[i] - '0';
        }
        if (index == 0 || index >= SCOPE_RATE_COUNT) {
            return reply_write_text(reply, "ERR rate index\n");
        }
        if (scope_state.configuration.acquisition.horizontal_enabled) {
            struct scope_acquisition_geometry geometry;
            if (!scope_acquisition_geometry_ratio(
                    sample_rate_numerator(index),
                    sample_rate_denominator(index),
                    10000,
                    scope_state.configuration.acquisition.horizontal_ns,
                    &geometry)) {
                return reply_write_text(reply, "ERR horizontal offset outside new timebase\n");
            }
        }
        if (scope_state.capture.state == CAPTURE_CONFIGURING ||
            scope_state.capture.state == CAPTURE_WAITING ||
            scope_state.capture.state == CAPTURE_TRANSFERRING) {
            if (scope_state.capture.live_enabled) {
                scope_state.capture.pending_rate_index = index;
                capture_abort_wait();
                return reply_write_text(reply, "OK capture rate index set\n");
            }
            return reply_write_text(reply, "ERR capture busy\n");
        }
        scope_state.capture.rate_index = index;
        return reply_write_text(reply, "OK capture rate index set\n");
    }
    if (command_equals(command, length, "CAPTURE TEST")) {
        if (scope_state.adc.state == SETUP_RUNNING) {
            return reply_write_text(reply, "ERR ADC initialization busy\n");
        }
        if (scope_state.analog.state == SETUP_RUNNING) {
            return reply_write_text(reply, "ERR analog initialization busy\n");
        }
        if (scope_state.fpga.state != FPGA_CONFIG_READY || !scope_state.fpga.spi_ready) {
            return reply_write_text(reply, "ERR frontend not ready\n");
        }
        if (scope_state.capture.state == CAPTURE_CONFIGURING ||
            scope_state.capture.state == CAPTURE_WAITING ||
            scope_state.capture.state == CAPTURE_TRANSFERRING) {
            return reply_write_text(reply, "ERR capture busy\n");
        }
        capture_start();
        return reply_write_text(reply, "OK capture test started\n");
    }
    if (command_equals(command, length, "CAPTURE STATUS")) {
        unsigned reply_length = reply_write_text(reply, "CAP state=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.capture.state);
        reply_length += reply_write_text(reply + reply_length, " error=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.capture.error);
        reply_length += reply_write_text(reply + reply_length, " flags=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.capture.csi_flags);
        reply_length += reply_write_text(reply + reply_length, " changed=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.capture.changed_samples);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (length == 17 && command[0] == 'C' && command[1] == 'A' && command[2] == 'P' &&
        command[3] == 'T' && command[4] == 'U' && command[5] == 'R' && command[6] == 'E' &&
        command[7] == ' ' && command[8] == 'R' && command[9] == 'E' && command[10] == 'A' &&
        command[11] == 'D' && command[12] == ' ') {
        unsigned offset = 0;
        for (unsigned i = 13; i < 17; i++) {
            unsigned character = command[i],
                     value = character >= '0' && character <= '9'   ? character - '0'
                             : character >= 'A' && character <= 'F' ? character - 'A' + 10
                             : character >= 'a' && character <= 'f' ? character - 'a' + 10
                                                                    : 16;
            if (value == 16) {
                return reply_write_text(reply, "ERR hex offset\n");
            }
            offset = (offset << 4) | value;
        }
        if (scope_state.capture.live_enabled) {
            return reply_write_text(reply, "ERR use FRAME during live capture\n");
        }
        if (scope_state.capture.state != CAPTURE_COMPLETE) {
            return reply_write_text(reply, "ERR no completed capture\n");
        }
        if (offset > 3968) {
            return reply_write_text(reply, "ERR capture offset\n");
        }
        unsigned reply_length = reply_write_text(reply, "DATA ");
        for (unsigned i = 0; i < 32; i++) {
            unsigned mask_bit = CSI_SAMPLE_BUFFER[offset + i];
            reply[reply_length++] = "0123456789ABCDEF"[mask_bit >> 4];
            reply[reply_length++] = "0123456789ABCDEF"[mask_bit & 15];
        }
        reply[reply_length++] = '\n';
        return reply_length;
    }
    return 0;
}

unsigned view_command(const uint8_t *command, unsigned length, uint8_t *reply) {
    if (command_equals(command, length, "VIEW STATUS")) {
        unsigned reply_length = reply_write_text(reply, "VIEW start=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.display.view_start);
        reply_length += reply_write_text(reply + reply_length, " points=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.display.view_points);
        reply_length += reply_write_text(reply + reply_length, " divisions=");
        reply_length += reply_write_hex32(reply + reply_length, DISPLAY_GRID_COLUMNS);
        reply_length += reply_write_text(reply + reply_length, " available=");
        reply_length += reply_write_hex32(reply + reply_length, 1998);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (length > 12 && command_equals(command, 12, "VIEW WINDOW ")) {
        unsigned split = 12;
        while (split < length && command[split] != ' ') {
            split++;
        }
        unsigned start, points;
        if (split == length || !command_parse_unsigned(command + 12, split - 12, &start) ||
            !command_parse_unsigned(command + split + 1, length - split - 1, &points) ||
            points < 10 || points > 2000) {
            return reply_write_text(reply, "ERR view window\n");
        }
        if (points == 2000 && start == 0) {
            points = 1998;
        }
        if (points > 1998 || start > 1998 - points) {
            return reply_write_text(reply, "ERR view window\n");
        }
        scope_state.display.view_start = start;
        scope_state.display.view_points = points;
        if (scope_state.display.screen_on && scope_state.frame.sequence) {
            scope_state.display.display_redraw_requested = 1;
        }
        return reply_write_text(reply, "OK view window set\n");
    }
    return 0;
}

unsigned record_command(const uint8_t *command, unsigned length, uint8_t *reply) {
    if (command_equals(command, length, "RECORD STATUS")) {
        unsigned reply_length = reply_write_text(reply, "REC state=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.capture.state);
        reply_length += reply_write_text(reply + reply_length, " busy=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.record.active);
        reply_length += reply_write_text(reply + reply_length, " ready=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.record.ready);
        reply_length += reply_write_text(reply + reply_length, " error=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.capture.error);
        reply_length += reply_write_text(reply + reply_length, " depth=");
        reply_length += reply_write_hex32(reply + reply_length,
                                          scope_state.configuration.acquisition.record_depth);
        reply_length += reply_write_text(reply + reply_length, " channels=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.record.channel_mask);
        reply_length += reply_write_text(reply + reply_length, " sequence=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.record.sequence);
        reply_length += reply_write_text(reply + reply_length, " forced=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.record.forced);
        reply_length += reply_write_text(reply + reply_length, " filling=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.record.filling);
        reply_length += reply_write_text(reply + reply_length, " reading=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.record.reading);
        unsigned elapsed_ms = timer_elapsed_ms(scope_state.record.fill_started_ms);
        reply_length += reply_write_text(reply + reply_length, " remaining_ms=");
        reply_length +=
            reply_write_hex32(reply + reply_length,
                              scope_state.record.filling && elapsed_ms < scope_state.record.fill_ms
                                  ? scope_state.record.fill_ms - elapsed_ms
                                  : 0);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command, length, "RECORD CANCEL")) {
        if (scope_state.record.active) {
            capture_stop();
            scope_state.capture.state = CAPTURE_IDLE;
            scope_state.record.active = scope_state.record.ready = scope_state.record.filling =
                scope_state.record.reading = scope_state.trigger.force_requested = 0;
            for (unsigned i = 0; i < 6; i++) {
                if (fpga_write_register(CAP_ACK, 1)) {
                    scope_state.capture.error = 1;
                    scope_state.capture.state = CAPTURE_FAILED;
                    return reply_write_text(reply, "ERR record cancellation transport\n");
                }
            }
        }
        return reply_write_text(reply, "OK record cancelled\n");
    }
    if (scope_state.record.active &&
        ((length >= 7 && command_equals(command, 7, "RECORD ")) ||
         (length >= 11 && command_equals(command, 11, "HORIZONTAL ")))) {
        return reply_write_text(reply, "ERR record acquisition busy\n");
    }
    if (command_equals(command, length, "RECORD DEPTH 1000") ||
        command_equals(command, length, "RECORD DEPTH 10000")) {
        if (capture_busy() || scope_state.capture.live_enabled) {
            return reply_write_text(reply, "ERR stop acquisition first\n");
        }
        scope_state.configuration.acquisition.record_depth = length == 18 ? 10000 : 1000;
        return reply_write_text(reply, "OK record depth set\n");
    }
    if (command_equals(command, length, "RECORD START CH1") ||
        command_equals(command, length, "RECORD START CH2") ||
        command_equals(command, length, "RECORD START BOTH")) {
        if (capture_busy() || scope_state.capture.live_enabled ||
            scope_state.analog.state == SETUP_RUNNING || scope_state.adc.state != SETUP_COMPLETE ||
            !scope_state.adc.mode || !scope_state.analog.configured ||
            scope_state.fpga.state != FPGA_CONFIG_READY || !scope_state.capture.normal_mode) {
            return reply_write_text(reply, "ERR stop normal acquisition first\n");
        }
        struct scope_acquisition_geometry geometry;
        unsigned depth = scope_state.configuration.acquisition.record_depth;
        if (!scope_acquisition_geometry_ratio(
                sample_rate_numerator(scope_state.capture.rate_index),
                sample_rate_denominator(scope_state.capture.rate_index),
                depth,
                scope_state.configuration.acquisition.horizontal_enabled
                    ? scope_state.configuration.acquisition.horizontal_ns
                    : 0,
                &geometry)) {
            return reply_write_text(reply, "ERR horizontal offset outside record\n");
        }
        scope_state.record.ready = 0;
        scope_state.record.active = 1;
        scope_state.record.channel = length == 17 ? 0 : command[length - 1] - '1';
        scope_state.record.channel_mask = length == 17 ? 3 : 1u << scope_state.record.channel;
        if (!scope_state.pulse.native && scope_state.trigger.advanced.mode == 2 &&
            !scope_state.configuration.features.trigger_family) {
            scope_state.record.channel_mask |= 1u << scope_state.trigger.configured.source;
        }
        scope_state.record.channel = scope_state.record.channel_mask & 1 ? 0 : 1;
        scope_state.record.first_channel = scope_state.record.channel;
        scope_state.record.sequence++;
        scope_state.record.depth = depth;
        capture_start();
        return reply_write_text(reply, "OK record acquisition started\n");
    }
    if (command_equals(command, length, "HORIZONTAL STATUS")) {
        unsigned reply_length = reply_write_text(reply, "HORIZONTAL enabled=");
        reply_length += reply_write_hex32(reply + reply_length,
                                          scope_state.configuration.acquisition.horizontal_enabled);
        reply_length += reply_write_text(reply + reply_length, " offset_ns=");
        reply_length += reply_write_hex32(
            reply + reply_length, (uint32_t)scope_state.configuration.acquisition.horizontal_ns);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command, length, "HORIZONTAL STOCK")) {
        scope_state.configuration.acquisition.horizontal_enabled = 0;
        capture_abort_wait();
        return reply_write_text(reply, "OK stock horizontal layout restored\n");
    }
    if (length > 18 && command_equals(command, 18, "HORIZONTAL OFFSET ")) {
        int32_t value;
        struct scope_acquisition_geometry geometry;
        if (!command_parse_signed(command + 18, length - 18, &value) || value < -1000000000 ||
            value > 1000000000 ||
            !scope_acquisition_geometry_ratio(
                sample_rate_numerator(scope_state.capture.rate_index),
                sample_rate_denominator(scope_state.capture.rate_index),
                10000,
                value,
                &geometry)) {
            return reply_write_text(reply, "ERR horizontal offset outside record\n");
        }
        scope_state.configuration.acquisition.horizontal_ns = value;
        scope_state.configuration.acquisition.horizontal_enabled = 1;
        capture_abort_wait();
        return reply_write_text(reply, "OK horizontal offset set\n");
    }
    return 0;
}
