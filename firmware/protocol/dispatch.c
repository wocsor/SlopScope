#include "protocol.h"
extern unsigned reply_write_hex32(uint8_t *reply, uint32_t value);
extern unsigned scope_command(const uint8_t *, unsigned, uint8_t *);
static int equals(const uint8_t *command, unsigned length, const char *literal) {
    unsigned i = 0;
    while (literal[i]) {
        if (i >= length || command[i] != (uint8_t)literal[i]) {
            return 0;
        }
        i++;
    }
    return i == length;
}
static unsigned reply_write_text(uint8_t *reply, const char *text) {
    unsigned reply_length = 0;
    while (text[reply_length]) {
        reply[reply_length] = (uint8_t)text[reply_length];
        reply_length++;
    }
    return reply_length;
}
unsigned command_handle(const uint8_t *line, unsigned length, uint8_t *reply, unsigned *action) {
    *action = ACTION_NONE;
    if (length > CONTROL_LINE_MAX) {
        return reply_write_text(reply, "ERR line too long\n");
    }
    if (equals(line, length, "SESSION OPEN") || equals(line, length, "SESSION PING")) {
        usb_session_open();
        return reply_write_text(reply, "OK SESSION OPEN\n");
    }
    if (equals(line, length, "SESSION CLOSE")) {
        usb_session_close();
        return reply_write_text(reply, "OK SESSION CLOSED\n");
    }
    if (equals(line, length, "SESSION STATUS")) {
        unsigned flags = usb_activity_flags();
        unsigned reply_length = reply_write_text(reply, "SESSION active=");
        reply_length += reply_write_hex32(reply + reply_length, !!(flags & USB_CLIENT_CONNECTED));
        reply_length += reply_write_text(reply + reply_length, " tx=");
        reply_length += reply_write_hex32(reply + reply_length, !!(flags & USB_TRANSMIT_ACTIVE));
        reply_length += reply_write_text(reply + reply_length, " rx=");
        reply_length += reply_write_hex32(reply + reply_length, !!(flags & USB_RECEIVE_ACTIVE));
        reply_length += reply_write_text(reply + reply_length, " lease_ms=");
        reply_length += reply_write_hex32(reply + reply_length, USB_SESSION_LEASE_MS);
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (equals(line, length, "PING")) {
        return reply_write_text(reply, "PONG\n");
    }
    if (equals(line, length, "INFO")) {
        return reply_write_text(reply, "SlopScope DOS1102 DEAF:BABE scope-control v2\n");
    }
    if (equals(line, length, "REBOOT FEL")) {
        *action = ACTION_FEL;
        return reply_write_text(reply, "OK REBOOT FEL\n");
    }
    if (equals(line, length, "REBOOT NORMAL")) {
        *action = ACTION_NORMAL;
        return reply_write_text(reply, "OK REBOOT NORMAL\n");
    }
    if (length >= 5 && line[0] == 'E' && line[1] == 'C' && line[2] == 'H' && line[3] == 'O' &&
        line[4] == ' ') {
        unsigned echo_bytes = length - 5;
        for (unsigned i = 0; i < echo_bytes; i++) {
            reply[i] = line[i + 5];
        }
        reply[echo_bytes++] = '\n';
        return echo_bytes;
    }
    unsigned frontend_length = scope_command(line, length, reply);
    if (frontend_length) {
        return frontend_length;
    }
    return reply_write_text(reply, "ERR unknown command\n");
}
