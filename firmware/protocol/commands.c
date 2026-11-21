#include "front_panel.h"
#include "panel_tools.h"
#include "scope_internal.h"
#include "screenshot.h"

static unsigned control_revision;

void scope_controls_changed(void) {
    control_revision++;
    scope_state.display.display_redraw_requested = 1;
}

static unsigned scope_command_dispatch(const uint8_t *command, unsigned length, uint8_t *reply);

static unsigned scope_command_dispatch(const uint8_t *command, unsigned length, uint8_t *reply) {
    unsigned screenshot_reply_length = screenshot_command(command, length, reply);
    if (screenshot_reply_length) {
        return screenshot_reply_length;
    }
    unsigned tools_reply_length = panel_tools_command(command, length, reply);
    if (tools_reply_length) {
        return tools_reply_length;
    }
    unsigned panel_reply_length = front_panel_command(command, length, reply);
    if (panel_reply_length) {
        return panel_reply_length;
    }
    unsigned autoset_reply_length = panel_autoset_command(command, length, reply);
    if (autoset_reply_length) {
        return autoset_reply_length;
    }
    if (command_equals(command, length, "UI REVISION")) {
        unsigned values[] = {control_revision,
                             scope_settings_sequence(),
                             scope_state.settings.restore_state,
                             scope_state.settings.dirty,
                             scope_state.settings.save_error,
                             panel_autoset_state()};
        const char *names[] = {
            "UI controls=", " saved=", " restore=", " dirty=", " save_error=", " autoset="};
        unsigned reply_length = 0;
        for (unsigned index = 0; index < sizeof(values) / sizeof(values[0]); index++) {
            reply_length += reply_write_text(reply + reply_length, names[index]);
            reply_length += reply_write_hex32(reply + reply_length, values[index]);
        }
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command, length, "CPU STATUS")) {
        unsigned control;
        __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(control));
        unsigned reply_length = reply_write_text(reply, "CPU control=");
        reply_length += reply_write_hex32(reply + reply_length, control);
        reply_length += reply_write_text(reply + reply_length, " clock=");
        reply_length += reply_write_hex32(reply + reply_length, MMIO32(0x01c20050u));
        reply_length += reply_write_text(reply + reply_length, " pll=");
        reply_length += reply_write_hex32(reply + reply_length, MMIO32(0x01c20000u));
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command, length, "CAPABILITIES")) {
        unsigned reply_length = reply_write_text(reply, "CAPS frame=");
        reply_length += reply_write_hex32(reply + reply_length, 7);
        reply_length += reply_write_text(reply + reply_length, " frame_chunks=");
        reply_length += reply_write_hex32(reply + reply_length, 1);
        reply_length += reply_write_text(reply + reply_length, " panel=");
        reply_length += reply_write_hex32(reply + reply_length, 2);
        reply_length += reply_write_text(reply + reply_length, " autoset=");
        reply_length += reply_write_hex32(reply + reply_length, 1);
        reply_length += reply_write_text(reply + reply_length, " screenshot=");
        reply_length += reply_write_hex32(reply + reply_length, 1);
        reply_length += reply_write_text(reply + reply_length, " record=");
        reply_length += reply_write_hex32(reply + reply_length, 4);
        reply_length += reply_write_text(reply + reply_length, " rates=");
        reply_length += reply_write_hex32(reply + reply_length, SCOPE_RATE_COUNT - 1);
        reply_length += reply_write_text(reply + reply_length, " native_verified=");
        reply_length += reply_write_hex32(reply + reply_length, 1);
        reply_length += reply_write_text(reply + reply_length, " sampled=");
        reply_length += reply_write_hex32(reply + reply_length, 4);
        reply_length += reply_write_text(reply + reply_length, " native_experimental=");
        reply_length += reply_write_hex32(reply + reply_length, 254);
        reply_length += reply_write_text(reply + reply_length, " session=");
        reply_length += reply_write_hex32(reply + reply_length, 1);
        reply_length += reply_write_text(reply + reply_length, " reply_max=");
        reply_length += reply_write_hex32(reply + reply_length, CONTROL_REPLY_MAX);
        reply_length += reply_write_text(reply + reply_length, " adc_gain=");
        reply_length += reply_write_hex32(reply + reply_length, 1);
        reply_length += reply_write_text(reply + reply_length, " video_controls=");
        reply_length += reply_write_hex32(reply + reply_length, 1);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command, length, "USB STATE")) {
        unsigned status_values[22];
        usb_transport_state(status_values);
        const char *names[] = {"USB power=",
                               " config=",
                               " mps=",
                               " ep0=",
                               " tx=",
                               " rx=",
                               " rx_count=",
                               " reply_left=",
                               " packet_left=",
                               " line=",
                               " max_poll_us=",
                               " clear_in=",
                               " clear_out=",
                               " recent_poll_us=",
                               " gaps_over_10ms=",
                               " suspends=",
                               " resumes=",
                               " disconnects=",
                               " bus_resets=",
                               " setups=",
                               " commands=",
                               " errors="};
        unsigned reply_length = 0;
        for (unsigned i = 0; i < 22; i++) {
            reply_length += reply_write_text(reply + reply_length, names[i]);
            reply_length += reply_write_hex32(reply + reply_length, status_values[i]);
        }
        reply[reply_length++] = '\n';
        return reply_length;
    }

    unsigned record_reply_length = record_command(command, length, reply);
    if (record_reply_length) {
        return record_reply_length;
    }
    if (scope_state.record.active && !(command_equals(command, length, "LIVE OFF") ||
                                       command_equals(command, length, "LIVE STATUS") ||
                                       command_equals(command, length, "ACQUISITION STATUS") ||
                                       command_equals(command, length, "CAPTURE STATUS") ||
                                       command_equals(command, length, "CAPTURE CONFIG") ||
                                       command_equals(command, length, "ROLL STATUS") ||
                                       command_equals(command, length, "USB SPEED") ||
                                       command_equals(command, length, "TRIGGER HARDWARE"))) {
        if (command_equals(command, length, "TRIGGER FORCE")) {
            scope_state.trigger.force_requested = 1;
            return reply_write_text(reply, "OK trigger force pending\n");
        }
        return reply_write_text(reply, "ERR record acquisition busy\n");
    }
    unsigned stored = settings_command(command, length, reply);
    if (stored) {
        return stored;
    }
    if (command_equals(command, length, "VOLTAGE MODEL")) {
        return reply_write_text(reply, "VOLTAGE version=00000002\n");
    }
    if (command_equals(command, length, "USB DEBUG")) {
        unsigned status_values[9];
        usb_debug(status_values);
        const char *names[] = {
            "USB p=", " fs=", " rst=", " pre_p=", " hs=", " pre_r=", " pre_s=", " e=", " c="};
        unsigned reply_length = 0;
        for (unsigned i = 0; i < 9; i++) {
            reply_length += reply_write_text(reply + reply_length, names[i]);
            reply_length += reply_write_hex32(reply + reply_length, status_values[i]);
        }
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command, length, "USB SPEED")) {
        return reply_write_text(
            reply, usb_high_speed() ? "USB high-speed bulk=512\n" : "USB full-speed bulk=64\n");
    }
    if (command_equals(command, length, "LIVE OFF")) {
        scope_state.capture.live_enabled = 0;
        scope_state.trigger.force_requested = 0;
        capture_abort_wait();
        if (scope_state.display.screen_on && scope_state.display.draw_buffer_valid) {
            scope_state.display.display_redraw_requested = 1;
        }
        return reply_write_text(reply, "OK live stopped\n");
    }
    if (command_equals(command, length, "LIVE ON")) {
        return capture_begin_live(0, reply);
    }
    if (command_equals(command, length, "LIVE RUN")) {
        return capture_begin_live(1, reply);
    }
    if (command_equals(command, length, "ACQUISITION STATUS")) {
        unsigned elapsed_ms = scope_state.capture.capture_roll
                                  ? timer_elapsed_ms(scope_state.capture.roll_fill_started_ms)
                              : capture_busy()
                                  ? timer_elapsed_ms(scope_state.capture.setup_started_ms)
                                  : 0,
                 sample_rate_hz = sample_rate_numerator(scope_state.capture.rate_index),
                 total = scope_state.capture.capture_roll ? 1998
                         : scope_state.record.active      ? scope_state.record.depth
                                                          : 10000;
        unsigned filled = (unsigned)scope_ratio(
            (int64_t)elapsed_ms * sample_rate_hz,
            (uint64_t)1000 * sample_rate_denominator(scope_state.capture.rate_index));
        if (filled > total ||
            (scope_state.capture.capture_roll && !scope_state.capture.roll_filling)) {
            filled = total;
        }
        unsigned reply_length = reply_write_text(reply, "ACT run=");
        reply_length += reply_write_hex32(
            reply + reply_length, scope_state.capture.live_enabled || scope_state.record.active);
        reply_length += reply_write_text(reply + reply_length, " state=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.capture.state);
        reply_length += reply_write_text(reply + reply_length, " elapsed_ms=");
        reply_length += reply_write_hex32(reply + reply_length, elapsed_ms);
        reply_length += reply_write_text(reply + reply_length, " filled=");
        reply_length += reply_write_hex32(reply + reply_length, filled);
        reply_length += reply_write_text(reply + reply_length, " total=");
        reply_length += reply_write_hex32(reply + reply_length, total);
        reply_length += reply_write_text(reply + reply_length, " roll=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.capture.capture_roll);
        reply_length += reply_write_text(reply + reply_length, " filling=");
        reply_length += reply_write_hex32(
            reply + reply_length, scope_state.capture.roll_filling || scope_state.record.filling);
        reply_length += reply_write_text(reply + reply_length, " record=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.record.active);
        reply_length += reply_write_text(reply + reply_length, " heartbeat=");
        reply_length +=
            reply_write_hex32(reply + reply_length, scope_state.display.activity_heartbeat);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command, length, "LIVE STATUS")) {
        unsigned reply_length = reply_write_text(reply, "LIVE enabled=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.capture.live_enabled);
        reply_length += reply_write_text(reply + reply_length, " sequence=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.frame.sequence);
        reply_length += reply_write_text(reply + reply_length, " busy=");
        reply_length += reply_write_hex32(reply + reply_length, capture_busy());
        reply_length += reply_write_text(reply + reply_length, " epoch=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.capture.live_epoch);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    unsigned view = view_command(command, length, reply);
    if (view) {
        return view;
    }
    unsigned trig = trigger_command(command, length, reply);
    if (trig) {
        return trig;
    }
    unsigned channel = channel_command(command, length, reply);
    if (channel) {
        return channel;
    }
    unsigned analog = analog_command(command, length, reply);
    if (analog) {
        return analog;
    }
    unsigned adc_reply_length = adc_command(command, length, reply);
    if (adc_reply_length) {
        return adc_reply_length;
    }
    unsigned capture_reply_length = capture_command(command, length, reply);
    if (capture_reply_length) {
        return capture_reply_length;
    }
    if (command_equals(command, length, "FRONTEND STATUS")) {
        unsigned reply_length = reply_write_text(reply, "FE state=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.fpga.state);
        reply_length += reply_write_text(reply + reply_length, " bytes=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.fpga.bytes_sent);
        reply_length += reply_write_text(reply + reply_length, " error=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.fpga.error);
        reply_length += reply_write_text(reply + reply_length, " pins=");
        reply_length +=
            reply_write_hex32(reply + reply_length,
                              gpio_read(GPIOF_BASE, 0) | (gpio_read(GPIOD_BASE, 12) << 1) |
                                  (gpio_read(GPIOE_BASE, 11) << 2));
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command, length, "FRONTEND LOAD") ||
        command_equals(command, length, "FRONTEND LOAD SLOW")) {
        if (scope_state.analog.state == SETUP_RUNNING || scope_state.adc.state == SETUP_RUNNING ||
            scope_state.capture.state == CAPTURE_CONFIGURING ||
            scope_state.capture.state == CAPTURE_WAITING ||
            scope_state.capture.state == CAPTURE_TRANSFERRING) {
            return reply_write_text(reply, "ERR frontend busy\n");
        }
        if (scope_state.display.screen_on) {
            return reply_write_text(reply,
                                    "ERR screen uses FPGA DONE pin; reboot FEL before loading\n");
        }
        if (((scope_state.fpga.state >= FPGA_CONFIG_HOLD_RESET &&
              scope_state.fpga.state <= FPGA_CONFIG_WAIT_DONE) ||
             scope_state.fpga.state == FPGA_CONFIG_FINISH_RESET)) {
            return reply_write_text(reply, "ERR frontend busy\n");
        }
        if (MMIO32(0x800a0104u) != 0x46504741u) {
            return reply_write_text(reply, "ERR FPGA payload not loaded\n");
        }
        scope_state.fpga.slow_clock = length == 18;
        scope_state.analog.configured = scope_state.capture.live_enabled = 0;
        scope_state.fpga.bytes_sent = scope_state.fpga.error = scope_state.fpga.spi_ready = 0;
        fpga_prepare_gpio();
        gpio_write(GPIOF_BASE, 1, 1);
        gpio_write(GPIOE_BASE, 11, 0);
        gpio_write(GPIOE_BASE, 12, 0);
        gpio_write(GPIOF_BASE, 1, 0);
        scope_state.fpga.started_ticks = timer_ticks();
        scope_state.fpga.state = FPGA_CONFIG_HOLD_RESET;
        return reply_write_text(reply, "OK FPGA loading\n");
    }
#if SCOPE_TRIGGER_RESEARCH
    /* RAM bench builds only: no analog, ADC, storage or FPGA configuration access */
    if (length == 37 && command_equals(command, 24, "FRONTEND RESEARCH WRITE ") &&
        command[28] == ' ') {
        unsigned address = 0, value = 0;
        for (unsigned i = 24; i < 37; i++) {
            if (i == 28) {
                continue;
            }
            unsigned character = command[i],
                     hex_digit = character >= '0' && character <= '9'   ? character - '0'
                                 : character >= 'a' && character <= 'f' ? character - 'a' + 10
                                 : character >= 'A' && character <= 'F' ? character - 'A' + 10
                                                                        : 16;
            if (hex_digit == 16) {
                return reply_write_text(reply, "ERR hex\n");
            }
            if (i < 28) {
                address = (address << 4) | hex_digit;
            } else {
                value = (value << 4) | hex_digit;
            }
        }
        if (scope_state.fpga.state != FPGA_CONFIG_READY || !scope_state.fpga.spi_ready) {
            return reply_write_text(reply, "ERR frontend not ready\n");
        }
        if (address != 0x300 && address != 0x31c && address != 0x32c && address != 0x320 &&
            address != 0x324 && address != 0x330 && address != 0x328 && address != 0x8c &&
            address != 0x58) {
            return reply_write_text(reply, "ERR research register\n");
        }
        if (fpga_write_register(address, value)) {
            return reply_write_text(reply, "ERR SPI timeout\n");
        }
        return reply_write_text(reply, "OK research write\n");
    }
#endif
    if (length == 18 && command[0] == 'F' && command[1] == 'R' && command[2] == 'O' &&
        command[3] == 'N' && command[4] == 'T' && command[5] == 'E' && command[6] == 'N' &&
        command[7] == 'D' && command[8] == ' ' && command[9] == 'R' && command[10] == 'E' &&
        command[11] == 'A' && command[12] == 'D' && command[13] == ' ') {
        unsigned address = 0;
        for (unsigned i = 14; i < 18; i++) {
            unsigned character = command[i];
            unsigned hex_digit = character >= '0' && character <= '9'   ? character - '0'
                                 : character >= 'a' && character <= 'f' ? character - 'a' + 10
                                 : character >= 'A' && character <= 'F' ? character - 'A' + 10
                                                                        : 16;
            if (hex_digit == 16) {
                return reply_write_text(reply, "ERR hex address\n");
            }
            address = (address << 4) | hex_digit;
        }
        if (scope_state.fpga.state != FPGA_CONFIG_READY || !scope_state.fpga.spi_ready) {
            return reply_write_text(reply, "ERR frontend not ready\n");
        }
        if (address & 3) {
            return reply_write_text(reply, "ERR address must align to four bytes\n");
        }
        uint32_t value;
        if (fpga_read_register(address, &value)) {
            return reply_write_text(reply, "ERR SPI timeout\n");
        }
        unsigned reply_length = reply_write_text(reply, "FE value=");
        reply_length += reply_write_hex32(reply + reply_length, value);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (command_equals(command, length, "SCREEN ON")) {
        if (((scope_state.fpga.state >= FPGA_CONFIG_HOLD_RESET &&
              scope_state.fpga.state <= FPGA_CONFIG_WAIT_DONE) ||
             scope_state.fpga.state == FPGA_CONFIG_FINISH_RESET)) {
            return reply_write_text(reply, "ERR frontend busy\n");
        }
        unsigned logo = MMIO32(0x800a0108u) == 0x4c4f474fu;
        if (scope_state.boot.console_active) {
        } else if (logo) {
            volatile uint16_t *destination = (volatile uint16_t *)DISPLAY_FRAMEBUFFER_0;
            const volatile uint16_t *source = (const volatile uint16_t *)0x81b00000u;
            for (unsigned i = 0; i < 800u * 480u; i++) {
                destination[i] = source[i];
            }
        } else {
            display_color_bars((uint16_t *)DISPLAY_FRAMEBUFFER_0);
        }
        if (!scope_state.boot.console_active) {
            display_prepare_from_fel();
            display_init_800x480(DISPLAY_FRAMEBUFFER_0);
            display_backlight(1);
            scope_state.display.displayed_buffer_index = 0;
        }
        scope_state.display.screen_on = 1;
        scope_state.display.draw_buffer_valid = scope_state.display.buffer_ready[0] =
            scope_state.display.buffer_ready[1] = 0;
        scope_state.display.draw_buffer_index = scope_state.display.displayed_buffer_index ^ 1u;
        scope_state.display.draw_framebuffer =
            (volatile uint16_t *)(uintptr_t)(scope_state.display.draw_buffer_index
                                                 ? DISPLAY_FRAMEBUFFER_1
                                                 : DISPLAY_FRAMEBUFFER_0);
        scope_state.display.display_redraw_requested = 1;
        return reply_write_text(reply, logo ? "OK screen boot logo\n" : "OK screen color bars\n");
    }
    return 0;
}

unsigned scope_command(const uint8_t *command, unsigned command_length, uint8_t *reply) {
    unsigned reply_length = scope_command_dispatch(command, command_length, reply);
    if (scope_state.boot.stage == BOOT_ACQUIRING && reply_length >= 3 && reply[0] == 'O' &&
        reply[1] == 'K' && reply[2] == ' ') {
        unsigned persistent =
            (command_length > 19 && command_equals(command, 19, "DISPLAY BRIGHTNESS ")) ||
            command_equals(command, command_length, "DISPLAY THEME LIGHT") ||
            command_equals(command, command_length, "DISPLAY THEME DARK") ||
            (command_length > 17 && command_equals(command, 17, "VOLTAGE BASELINE ")) ||
            (command_length > 8 && command_equals(command, 8, "ACQUIRE ")) ||
            (command_length > 11 && command_equals(command, 11, "HORIZONTAL ")) ||
            (command_length > 13 && command_equals(command, 13, "RECORD DEPTH ")) ||
            (command_length > 3 && command[0] == 'C' && command[1] == 'H') ||
            (command_length > 8 && command_equals(command, 8, "TRIGGER ") &&
             !command_equals(command, command_length, "TRIGGER FORCE")) ||
            (command_length > 8 && command_equals(command, 8, "CAPTURE ")) ||
            (command_length > 5 && command_equals(command, 5, "VIEW ")) ||
            command_equals(command, command_length, "LIVE ON") ||
            command_equals(command, command_length, "LIVE OFF") ||
            command_equals(command, command_length, "LIVE RUN");
        if (persistent) {
            scope_controls_changed();
        }
        if (persistent && !scope_state.settings.save_held) {
            scope_state.configuration.average_epoch++;
            scope_state.settings.dirty = 1;
            scope_state.settings.changed = timer_milliseconds();
            scope_state.settings.save_error = 0;
            scope_state.settings.save_delay = 2000;
        }
    }
    return reply_length;
}
