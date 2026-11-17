#include "scope_internal.h"

#define FRAME_CHUNK_PAYLOAD_BYTES 448u
#define FRAME_CHUNK_HEADER_BYTES 32u

static uint8_t pinned_frame[4124];
static unsigned pinned_length, pinned_sequence, transfer_id;

static int parse_transfer_id(const uint8_t *text, unsigned length, unsigned *value) {
    if (!length || length > 10) {
        return 0;
    }
    unsigned parsed = 0;
    for (unsigned index = 0; index < length; index++) {
        if (text[index] < '0' || text[index] > '9' ||
            parsed > (UINT32_MAX - (text[index] - '0')) / 10u) {
            return 0;
        }
        parsed = parsed * 10u + text[index] - '0';
    }
    *value = parsed;
    return 1;
}

static void write_word(uint8_t *destination, unsigned value) {
    for (unsigned byte = 0; byte < 4; byte++) {
        destination[byte] = value >> (byte * 8);
    }
}

static unsigned read_word(const uint8_t *source) {
    unsigned value = 0;
    for (unsigned byte = 0; byte < 4; byte++) {
        value |= (unsigned)source[byte] << (byte * 8);
    }
    return value;
}

void usb_frame_transfer_reset(void) {
    pinned_length = 0;
}

unsigned usb_frame_transfer_command(const uint8_t *command, unsigned length, uint8_t *reply) {
    if (command_equals(command, length, "FRAME OPEN")) {
        unsigned frame_bytes;
        const uint8_t *frame = scope_snapshot_v7(&frame_bytes);
        pinned_length = 0;
        if (!frame_bytes || frame_bytes > sizeof pinned_frame) {
            return reply_write_text(reply, "ERR no frame\n");
        }
        for (unsigned index = 0; index < frame_bytes; index++) {
            pinned_frame[index] = frame[index];
        }
        pinned_length = frame_bytes;
        pinned_sequence = read_word(frame + 16);
        if (!++transfer_id) {
            transfer_id++;
        }
        const char *names[] = {"FRAME id=", " bytes=", " sequence=", " chunk="};
        const unsigned values[] = {
            transfer_id, pinned_length, pinned_sequence, FRAME_CHUNK_PAYLOAD_BYTES};
        unsigned reply_bytes = 0;
        for (unsigned index = 0; index < 4; index++) {
            reply_bytes += reply_write_text(reply + reply_bytes, names[index]);
            reply_bytes += reply_write_hex32(reply + reply_bytes, values[index]);
        }
        reply[reply_bytes++] = '\n';
        return reply_bytes;
    }
    unsigned close = length > 12 && command_equals(command, 12, "FRAME CLOSE ");
    unsigned read = length > 11 && command_equals(command, 11, "FRAME READ ");
    if (!close && !read) {
        return 0;
    }
    unsigned argument_start = close ? 12 : 11, argument_end = argument_start;
    while (argument_end < length && command[argument_end] != ' ') {
        argument_end++;
    }
    unsigned requested_id;
    if (!parse_transfer_id(
            command + argument_start, argument_end - argument_start, &requested_id) ||
        !pinned_length || requested_id != transfer_id) {
        return reply_write_text(reply, "ERR stale frame transfer\n");
    }
    if (close) {
        if (argument_end != length) {
            return reply_write_text(reply, "ERR frame arguments\n");
        }
        pinned_length = 0;
        return reply_write_text(reply, "OK\n");
    }
    unsigned offset;
    if (argument_end == length ||
        !command_parse_unsigned(command + argument_end + 1, length - argument_end - 1, &offset) ||
        offset >= pinned_length) {
        return reply_write_text(reply, "ERR frame offset\n");
    }
    unsigned chunk_bytes = pinned_length - offset;
    if (chunk_bytes > FRAME_CHUNK_PAYLOAD_BYTES) {
        chunk_bytes = FRAME_CHUNK_PAYLOAD_BYTES;
    }
    const unsigned header[] = {
        0x314b4843u, transfer_id, pinned_sequence, offset, pinned_length, chunk_bytes, 1, 0};
    for (unsigned index = 0; index < 8; index++) {
        write_word(reply + index * 4, header[index]);
    }
    for (unsigned index = 0; index < chunk_bytes; index++) {
        reply[FRAME_CHUNK_HEADER_BYTES + index] = pinned_frame[offset + index];
    }
    unsigned reply_bytes = FRAME_CHUNK_HEADER_BYTES + chunk_bytes;
    write_word(reply + 28, scope_crc32(reply, reply_bytes));
    return reply_bytes;
}
