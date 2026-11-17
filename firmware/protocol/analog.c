#include "scope_internal.h"

unsigned analog_command(const uint8_t *command, unsigned length, uint8_t *reply) {
    if (command_equals(command, length, "ANALOG INIT")) {
        if (scope_state.fpga.state != FPGA_CONFIG_READY || !scope_state.fpga.spi_ready) {
            return reply_write_text(reply, "ERR frontend not ready\n");
        }
        if (scope_state.analog.state == SETUP_RUNNING || scope_state.adc.state == SETUP_RUNNING ||
            scope_state.capture.state == CAPTURE_CONFIGURING ||
            scope_state.capture.state == CAPTURE_WAITING ||
            scope_state.capture.state == CAPTURE_TRANSFERRING) {
            return reply_write_text(reply, "ERR frontend busy\n");
        }
        scope_state.analog.job_kind = 0;
        scope_state.analog.configured = scope_state.capture.live_enabled = 0;
        scope_state.analog.step_index = scope_state.analog.error = scope_state.analog.wait_ticks =
            0;
        scope_state.analog.state = SETUP_RUNNING;
        return reply_write_text(reply, "OK analog initialization started\n");
    }
    if (command_equals(command, length, "ANALOG STATUS")) {
        unsigned reply_length = reply_write_text(reply, "ANALOG state=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.analog.state);
        reply_length += reply_write_text(reply + reply_length, " event=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.analog.step_index);
        reply_length += reply_write_text(reply + reply_length, " error=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.analog.error);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    return 0;
}
