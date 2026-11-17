#include "scope_internal.h"

unsigned adc_command(const uint8_t *command, unsigned length, uint8_t *reply) {
    if (command_equals(command, length, "ADC TESTINIT") ||
        command_equals(command, length, "ADC NORMAL")) {
        if (scope_state.fpga.state != FPGA_CONFIG_READY || !scope_state.fpga.spi_ready) {
            return reply_write_text(reply, "ERR frontend not ready\n");
        }
        if (scope_state.analog.state == SETUP_RUNNING || scope_state.adc.state == SETUP_RUNNING ||
            scope_state.capture.state == CAPTURE_CONFIGURING ||
            scope_state.capture.state == CAPTURE_WAITING ||
            scope_state.capture.state == CAPTURE_TRANSFERRING) {
            return reply_write_text(reply, "ERR frontend busy\n");
        }
        scope_state.capture.live_enabled = 0;
        scope_state.adc.mode = command_equals(command, length, "ADC NORMAL");
        scope_state.adc.step_index = scope_state.adc.error = scope_state.adc.wait = 0;
        scope_state.adc.state = SETUP_RUNNING;
        return reply_write_text(reply,
                                scope_state.adc.mode ? "OK ADC normal mode started\n"
                                                     : "OK ADC test initialization started\n");
    }
    if (command_equals(command, length, "ADC STATUS")) {
        unsigned reply_length = reply_write_text(reply, "ADC state=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.adc.state);
        reply_length += reply_write_text(reply + reply_length, " event=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.adc.step_index);
        reply_length += reply_write_text(reply + reply_length, " error=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.adc.error);
        reply_length += reply_write_text(reply + reply_length, " mode=");
        reply_length += reply_write_hex32(reply + reply_length, scope_state.adc.mode);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    return 0;
}
