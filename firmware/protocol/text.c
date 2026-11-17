#include "scope_internal.h"

int command_equals(const uint8_t *command, unsigned length, const char *expected_command) {
    unsigned i = 0;
    while (expected_command[i]) {
        if (i >= length || command[i] != (uint8_t)expected_command[i]) {
            return 0;
        }
        i++;
    }
    return i == length;
}

unsigned reply_write_text(uint8_t *reply, const char *text) {
    unsigned reply_length = 0;
    while (*text) {
        reply[reply_length++] = (uint8_t)*text++;
    }
    return reply_length;
}

unsigned reply_write_hex32(uint8_t *reply, uint32_t value) {
    for (unsigned i = 0; i < 8; i++) {
        reply[i] = "0123456789ABCDEF"[(value >> (28 - i * 4)) & 15];
    }
    return 8;
}

int command_parse_unsigned(const uint8_t *text, unsigned length, unsigned *value) {
    if (!length || length > 5) {
        return 0;
    }
    unsigned parsed_value = 0;
    for (unsigned i = 0; i < length; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return 0;
        }
        parsed_value = parsed_value * 10 + text[i] - '0';
    }
    *value = parsed_value;
    return 1;
}

int command_parse_signed(const uint8_t *text, unsigned length, int32_t *value) {
    unsigned negative = length && text[0] == '-';
    if (negative) {
        text++;
        length--;
    }
    if (!length || length > 10) {
        return 0;
    }
    uint32_t parsed_magnitude = 0;
    for (unsigned i = 0; i < length; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return 0;
        }
        unsigned digit = text[i] - '0';
        if (parsed_magnitude > 214748364u || (parsed_magnitude == 214748364u && digit > 7)) {
            return 0;
        }
        parsed_magnitude = parsed_magnitude * 10 + digit;
    }
    *value = negative ? -(int32_t)parsed_magnitude : (int32_t)parsed_magnitude;
    return 1;
}
