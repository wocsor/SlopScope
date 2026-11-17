#include "scope_internal.h"

static unsigned trigger_bank_query(const uint8_t *command, unsigned length, uint8_t *reply);

static unsigned trigger_bank_query(const uint8_t *command, unsigned length, uint8_t *reply) {
    unsigned bank, skip;
    if (length > 12 && command_equals(command, 12, "BANK NORMAL ")) {
        bank = 2;
        skip = 12;
    } else if (length > 9 && command_equals(command, 7, "BANK CH") &&
               (command[7] == '1' || command[7] == '2') && command[8] == ' ') {
        bank = command[7] - '1';
        skip = 9;
    } else {
        return 0;
    }
    struct scope_trigger_bank current;
    const struct scope_trigger_bank *trigger_bank;
    if ((bank == 2 && !scope_state.configuration.features.trigger_family) ||
        (bank < 2 && scope_state.configuration.features.trigger_family &&
         scope_state.trigger.configured.source == bank)) {
        current.trigger = scope_state.trigger.configured;
        current.advanced = scope_state.trigger.advanced;
        trigger_bank = &current;
    } else {
        trigger_bank = bank == 2 ? &scope_state.configuration.features.normal
                                 : &scope_state.configuration.features.bank[bank];
    }
    const struct scope_trigger *trigger_config = &trigger_bank->trigger;
    const struct scope_advanced_trigger *advanced_trigger = &trigger_bank->advanced;
    const char *names[5];
    uint32_t values[5];
    unsigned count;
    command += skip;
    length -= skip;
    if (command_equals(command, length, "EDGE")) {
        count = 4;
        names[0] = "BANK slope=";
        values[0] = trigger_config->slope;
        names[1] = " uv=";
        values[1] = (uint32_t)trigger_config->level_uv;
        names[2] = " code=";
        values[2] = (uint32_t)trigger_config->code;
        names[3] = " raw=";
        values[3] = trigger_config->use_code;
    } else if (command_equals(command, length, "TIMING")) {
        count = 4;
        names[0] = "BANK src=";
        values[0] = trigger_config->source;
        names[1] = " sweep=";
        values[1] = trigger_config->sweep;
        names[2] = " coupling=";
        values[2] = trigger_config->coupling;
        names[3] = " holdoff_ns=";
        values[3] = trigger_config->holdoff_ns;
    } else if (command_equals(command, length, "MODE")) {
        count = 4;
        names[0] = "BANK mode=";
        values[0] = advanced_trigger->mode;
        names[1] = " condition=";
        values[1] = advanced_trigger->condition;
        names[2] = " width_ns=";
        values[2] = advanced_trigger->width_ns;
        names[3] = " count=";
        values[3] = advanced_trigger->count;
    } else if (command_equals(command, length, "LIMITS")) {
        count = 5;
        names[0] = "BANK lower_uv=";
        values[0] = (uint32_t)advanced_trigger->lower_uv;
        names[1] = " upper_uv=";
        values[1] = (uint32_t)advanced_trigger->upper_uv;
        names[2] = " video_count=";
        values[2] = advanced_trigger->video_count;
        names[3] = " sync=";
        values[3] = advanced_trigger->video_sync;
        names[4] = " standard=";
        values[4] = advanced_trigger->video_standard;
    } else {
        return reply_write_text(reply, "ERR trigger bank query\n");
    }
    unsigned reply_length = 0;
    for (unsigned i = 0; i < count; i++) {
        reply_length += reply_write_text(reply + reply_length, names[i]);
        reply_length += reply_write_hex32(reply + reply_length, values[i]);
    }
    reply[reply_length++] = '\n';
    return reply_length;
}

unsigned trigger_command(const uint8_t *command, unsigned length, uint8_t *reply) {
    if (length < 8 || !command_equals(command, 8, "TRIGGER ")) {
        return 0;
    }
    command += 8;
    length -= 8;
    if (command_equals(command, length, "HARDWARE")) {
        if (scope_state.fpga.state != FPGA_CONFIG_READY || !scope_state.fpga.spi_ready) {
            return reply_write_text(reply, "ERR frontend not ready\n");
        }
        /* The stock acquisition loop reads these three independent registers */
        uint32_t detected, event, stopped;
        if (fpga_read_register(0x40, &detected) || fpga_read_register(0x4c, &event) ||
            fpga_read_register(0x184, &stopped)) {
            return reply_write_text(reply, "ERR SPI timeout\n");
        }
        unsigned values[] = {scope_state.capture.live_enabled,
                             scope_state.capture.state,
                             scope_state.frame.sequence,
                             scope_state.capture.epoch,
                             gpio_read(GPIOE_BASE, 11),
                             detected,
                             event,
                             stopped,
                             scope_state.trigger.active_advanced.mode,
                             scope_state.pulse.native};
        const char *names[] = {"TRIGHW run=",
                               " state=",
                               " sequence=",
                               " epoch=",
                               " ready=",
                               " detected=",
                               " event=",
                               " stop=",
                               " mode=",
                               " native="};
        unsigned reply_length = 0;
        for (unsigned i = 0; i < 10; i++) {
            reply_length += reply_write_text(reply + reply_length, names[i]);
            reply_length += reply_write_hex32(reply + reply_length, values[i]);
        }
        reply[reply_length++] = '\n';
        return reply_length;
    }
    unsigned bank_reply = trigger_bank_query(command, length, reply);
    if (bank_reply) {
        return bank_reply;
    }
    if (command_equals(command, length, "PULSE BACKEND SAMPLED") ||
        command_equals(command, length, "PULSE BACKEND NATIVE")) {
        if (capture_busy() || scope_state.capture.live_enabled) {
            return reply_write_text(reply, "ERR stop acquisition before backend change\n");
        }
        scope_state.pulse.native = command_equals(command, length, "PULSE BACKEND NATIVE");
        return reply_write_text(reply, "OK session-only pulse backend set\n");
    }
    if (command_equals(command, length, "PULSE STATUS")) {
        unsigned reply_length = reply_write_text(reply, "PULSE sampled=");
        reply_length += reply_write_hex32(reply + reply_length, !scope_state.pulse.native);
        reply_length += reply_write_text(reply + reply_length, " candidates=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.pulse.candidates);
        reply_length += reply_write_text(reply + reply_length, " accepted=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.pulse.accepted);
        reply_length += reply_write_text(reply + reply_length, " rejected=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.pulse.rejected);
        reply_length += reply_write_text(reply + reply_length, " auto=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.pulse.auto_fallbacks);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command, length, "FAMILY")) {
        unsigned reply_length = reply_write_text(reply, "FAMILY alternate=");
        reply_length += reply_write_hex32(reply + reply_length,
                                          scope_state.configuration.features.trigger_family);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command, length, "FAMILY NORMAL") ||
        command_equals(command, length, "FAMILY ALTERNATE")) {
        unsigned alternate = command_equals(command, length, "FAMILY ALTERNATE");
        if (alternate && scope_state.trigger.configured.sweep != 0) {
            return reply_write_text(reply, "ERR select AUTO sweep before alternate triggering\n");
        }
        if (alternate != scope_state.configuration.features.trigger_family) {
            if (alternate) {
                scope_state.configuration.features.normal.trigger = scope_state.trigger.configured;
                scope_state.configuration.features.normal.advanced = scope_state.trigger.advanced;
                unsigned channel_index = scope_state.trigger.configured.source;
                scope_state.trigger.configured =
                    scope_state.configuration.features.bank[channel_index].trigger;
                scope_state.trigger.advanced =
                    scope_state.configuration.features.bank[channel_index].advanced;
            } else {
                trigger_bank_store();
                scope_state.trigger.configured = scope_state.configuration.features.normal.trigger;
                scope_state.trigger.advanced = scope_state.configuration.features.normal.advanced;
            }
            scope_state.configuration.features.trigger_family = alternate;
            capture_abort_wait();
            scope_state.display.display_redraw_requested = 1;
        }
        return reply_write_text(reply, "OK trigger family set\n");
    }
    if (scope_state.configuration.features.trigger_family &&
        (command_equals(command, length, "SOURCE CH1") ||
         command_equals(command, length, "SOURCE CH2"))) {
        trigger_bank_store();
        unsigned sweep = scope_state.trigger.configured.sweep,
                 channel_index = command[length - 1] - '1';
        scope_state.trigger.configured =
            scope_state.configuration.features.bank[channel_index].trigger;
        scope_state.trigger.configured.sweep = sweep;
        scope_state.trigger.advanced =
            scope_state.configuration.features.bank[channel_index].advanced;
        capture_abort_wait();
        scope_state.display.display_redraw_requested = 1;
        return reply_write_text(reply, "OK alternate bank selected\n");
    }

    if (scope_state.configuration.features.trigger_family &&
        (command_equals(command, length, "SWEEP NORMAL") ||
         command_equals(command, length, "SWEEP SINGLE"))) {
        return reply_write_text(reply, "ERR alternate triggering uses AUTO sweep\n");
    }
    if (scope_state.trigger.advanced.mode == 1 &&
        (command_equals(command, length, "SWEEP NORMAL") ||
         command_equals(command, length, "SWEEP SINGLE"))) {
        return reply_write_text(reply, "ERR video triggering uses AUTO sweep\n");
    }
    if (command_equals(command, length, "SINGLE")) {
        return capture_begin_live(2, reply);
    }
    if (command_equals(command, length, "STATUS")) {
        unsigned reply_length = reply_write_text(reply, "TRIG en=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.trigger.enabled);
        reply_length += reply_write_text(reply + reply_length, " src=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.trigger.configured.source);
        reply_length += reply_write_text(reply + reply_length, " slope=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.trigger.configured.slope);
        reply_length += reply_write_text(reply + reply_length, " sweep=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.trigger.configured.sweep);
        reply_length += reply_write_text(reply + reply_length, " uv=");
        reply_length += reply_write_hex32(reply + reply_length,
                                          (uint32_t)scope_state.trigger.configured.level_uv);
        reply_length += reply_write_text(reply + reply_length, " code=");
        reply_length +=
            reply_write_hex32(reply + reply_length, (uint32_t)scope_state.trigger.configured.code);
        reply_length += reply_write_text(reply + reply_length, " raw=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.trigger.configured.use_code);
        reply_length += reply_write_text(reply + reply_length, " forced=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.trigger.forced);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command, length, "DETAIL")) {
        unsigned reply_length = reply_write_text(reply, "TRIG coupling=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.trigger.configured.coupling);
        reply_length += reply_write_text(reply + reply_length, " holdoff_ns=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.trigger.configured.holdoff_ns);
        reply_length += reply_write_text(reply + reply_length, " active=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.trigger.active);
        reply_length += reply_write_text(reply + reply_length, " waiting=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.capture.state == CAPTURE_WAITING);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command, length, "FORCE")) {
        if (!scope_state.trigger.enabled || !scope_state.capture.normal_mode ||
            !scope_state.capture.live_enabled) {
            return reply_write_text(reply, "ERR start live trigger capture first\n");
        }
        scope_state.trigger.force_requested = 1;
        return reply_write_text(reply, "OK trigger force pending\n");
    }
    if (command_equals(command, length, "ADVANCED")) {
        unsigned reply_length = reply_write_text(reply, "ADV mode=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.trigger.advanced.mode);
        reply_length += reply_write_text(reply + reply_length, " condition=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.trigger.advanced.condition);
        reply_length += reply_write_text(reply + reply_length, " width_ns=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.trigger.advanced.width_ns);
        reply_length += reply_write_text(reply + reply_length, " count=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.trigger.advanced.count);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command, length, "LIMITS")) {
        unsigned reply_length = reply_write_text(reply, "ADV lower_uv=");
        reply_length += reply_write_hex32(reply + reply_length,
                                          (uint32_t)scope_state.trigger.advanced.lower_uv);
        reply_length += reply_write_text(reply + reply_length, " upper_uv=");
        reply_length += reply_write_hex32(reply + reply_length,
                                          (uint32_t)scope_state.trigger.advanced.upper_uv);
        reply_length += reply_write_text(reply + reply_length, " video_count=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.trigger.advanced.video_count);
        reply_length += reply_write_text(reply + reply_length, " sync=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.trigger.advanced.video_sync);
        reply_length += reply_write_text(reply + reply_length, " standard=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.trigger.advanced.video_standard);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    struct scope_advanced_trigger next = scope_state.trigger.advanced;
    unsigned extended = 1;
    int32_t value;
    if (length > 10 && command_equals(command, 10, "VIDEO SET ")) {
        unsigned standard_start = 10, standard_end = standard_start;
        while (standard_end < length && command[standard_end] != ' ') {
            standard_end++;
        }
        unsigned sync_start = standard_end + 1, sync_end = sync_start;
        while (sync_end < length && command[sync_end] != ' ') {
            sync_end++;
        }
        unsigned standard, sync, count;
        if (standard_end == length || sync_end == length ||
            !command_parse_unsigned(
                command + standard_start, standard_end - standard_start, &standard) ||
            !command_parse_unsigned(command + sync_start, sync_end - sync_start, &sync) ||
            !command_parse_unsigned(command + sync_end + 1, length - sync_end - 1, &count) ||
            standard > 2 || sync > 4 || count > 65535) {
            return reply_write_text(reply, "ERR video settings\n");
        }
        next.video_standard = standard;
        next.video_sync = sync;
        next.video_count = count;
    } else if (length > 7 && command_equals(command, 7, "LIMITS ")) {
        unsigned split = 7;
        while (split < length && command[split] != ' ') {
            split++;
        }
        if (split == length || !command_parse_signed(command + 7, split - 7, &next.lower_uv) ||
            !command_parse_signed(command + split + 1, length - split - 1, &next.upper_uv)) {
            return reply_write_text(reply, "ERR trigger levels\n");
        }
    } else if (length > 5 && command_equals(command, 5, "MODE ")) {
        static const char *modes[] = {
            "EDGE", "VIDEO", "PULSE", "SLOPE", "RUNT", "SPIKE", "TIMEOUT", "NTHEDGE"};
        unsigned i = 0;
        while (i < 8 && !command_equals(command + 5, length - 5, modes[i])) {
            i++;
        }
        if (i == 8) {
            return reply_write_text(reply, "ERR trigger mode\n");
        }
        next.mode = i;
    } else if (length > 10 && command_equals(command, 10, "CONDITION ") &&
               command_parse_signed(command + 10, length - 10, &value) && value >= 0) {
        next.condition = (unsigned)value;
    } else if (length > 6 && command_equals(command, 6, "WIDTH ") &&
               command_parse_signed(command + 6, length - 6, &value) && value >= 0) {
        next.width_ns = (unsigned)value;
    } else if (length > 6 && command_equals(command, 6, "LOWER ") &&
               command_parse_signed(command + 6, length - 6, &value)) {
        next.lower_uv = value;
    } else if (length > 6 && command_equals(command, 6, "UPPER ") &&
               command_parse_signed(command + 6, length - 6, &value)) {
        next.upper_uv = value;
    } else if (length > 6 && command_equals(command, 6, "COUNT ") &&
               command_parse_signed(command + 6, length - 6, &value) && value >= 0) {
        next.count = (unsigned)value;
    } else if (length > 12 && command_equals(command, 12, "VIDEO COUNT ") &&
               command_parse_signed(command + 12, length - 12, &value) && value >= 0) {
        next.video_count = (unsigned)value;
    } else if (length > 11 && command_equals(command, 11, "VIDEO SYNC ") &&
               command_parse_signed(command + 11, length - 11, &value) && value >= 0 &&
               value <= 4) {
        next.video_sync = (unsigned)value;
    } else if (length > 15 && command_equals(command, 15, "VIDEO STANDARD ") &&
               command_parse_signed(command + 15, length - 15, &value) && value >= 0) {
        next.video_standard = (unsigned)value;
    } else {
        extended = 0;
    }
    if (extended) {
        if (!scope_advanced_valid(&next)) {
            return reply_write_text(reply, "ERR advanced trigger argument\n");
        }
        video_control_normalize(&scope_state.trigger.configured, &next);
        scope_state.trigger.advanced = next;
        scope_state.trigger.force_requested = 0;
        capture_abort_wait();
        scope_state.display.display_redraw_requested = 1;
        return reply_write_text(reply, "OK advanced trigger updated\n");
    }
    unsigned previous_sweep = scope_state.trigger.configured.sweep;
    int32_t signed_value;
    if (command_equals(command, length, "ENABLE ON")) {
        scope_state.trigger.enabled = 1;
    } else if (command_equals(command, length, "ENABLE OFF")) {
        scope_state.trigger.enabled = 0;
    } else if (command_equals(command, length, "SOURCE CH1") ||
               command_equals(command, length, "SOURCE CH2")) {
        scope_state.trigger.configured.source = command[length - 1] - '1';
    } else if (command_equals(command, length, "SLOPE RISE") ||
               command_equals(command, length, "SLOPE FALL")) {
        scope_state.trigger.configured.slope = command[6] == 'F';
    } else if (command_equals(command, length, "SWEEP AUTO")) {
        scope_state.trigger.configured.sweep = 0;
    } else if (command_equals(command, length, "SWEEP NORMAL")) {
        scope_state.trigger.configured.sweep = 1;
    } else if (command_equals(command, length, "SWEEP SINGLE")) {
        scope_state.trigger.configured.sweep = 2;
    } else if (command_equals(command, length, "COUPLING DC") ||
               command_equals(command, length, "COUPLING AC")) {
        scope_state.trigger.configured.coupling = command[9] == 'A';
    } else if (length > 6 && command_equals(command, 6, "LEVEL ") &&
               command_parse_signed(command + 6, length - 6, &signed_value) &&
               signed_value >= -1000000000 && signed_value <= 1000000000) {
        scope_state.trigger.configured.level_uv = signed_value;
        scope_state.trigger.configured.use_code = 0;
    } else if (length > 5 && command_equals(command, 5, "CODE ") &&
               command_parse_signed(command + 5, length - 5, &signed_value) &&
               signed_value >= -127 && signed_value <= 126) {
        scope_state.trigger.configured.code = signed_value;
        scope_state.trigger.configured.use_code = 1;
    } else if (length > 8 && command_equals(command, 8, "HOLDOFF ") &&
               command_parse_signed(command + 8, length - 8, &signed_value) &&
               signed_value >= 100 && signed_value <= 1000000000) {
        scope_state.trigger.configured.holdoff_ns = (unsigned)signed_value;
    } else {
        return reply_write_text(reply, "ERR trigger argument\n");
    }
    if (previous_sweep != 2) {
        scope_state.capture.continuous_sweep = previous_sweep;
    }
    if (scope_state.trigger.configured.sweep != 2) {
        scope_state.capture.continuous_sweep = scope_state.trigger.configured.sweep;
    }
    scope_state.trigger.force_requested = 0;
    capture_abort_wait();
    if (scope_state.display.screen_on && scope_state.display.draw_buffer_valid) {
        scope_state.display.display_redraw_requested = 1;
    }
    return reply_write_text(reply, "OK trigger updated\n");
}
