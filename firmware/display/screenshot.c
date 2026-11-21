#include "screenshot.h"
#include "front_panel.h"
#include "scope_internal.h"

#define SCREENSHOT_BUFFER ((uint16_t *)0x81e00000u)
#define SCREENSHOT_BYTES (800u * 480u * 2u)
#define SCREENSHOT_CHUNK_BYTES 448u

static unsigned screenshot_row, copying, ready, screenshot_id;
static const volatile uint16_t *source_framebuffer;

unsigned screenshot_busy(void) {
    return copying;
}
unsigned screenshot_ready(void) {
    return ready;
}

void screenshot_begin(void) {
    if (!scope_state.display.screen_on || scope_state.boot.stage != BOOT_ACQUIRING) {
        return;
    }
    source_framebuffer =
        (const volatile uint16_t *)(uintptr_t)(scope_state.display.displayed_buffer_index
                                                   ? DISPLAY_FRAMEBUFFER_1
                                                   : DISPLAY_FRAMEBUFFER_0);
    screenshot_row = ready = 0;
    copying = 1;
    if (!++screenshot_id) {
        screenshot_id++;
    }
}

void screenshot_poll(void) {
    if (!copying) {
        return;
    }
    unsigned end = screenshot_row + 8;
    if (end > 480) {
        end = 480;
    }
    for (; screenshot_row < end; screenshot_row++) {
        for (unsigned x = 0; x < 800; x++) {
            SCREENSHOT_BUFFER[screenshot_row * 800 + x] =
                source_framebuffer[screenshot_row * 800 + x];
        }
        usb_service();
    }
    if (screenshot_row == 480) {
        ready = 1;
        copying = 0;
        front_panel_menu_refresh();
    }
}

static void screenshot_write_word(uint8_t *reply, uint32_t value) {
    for (unsigned byte = 0; byte < 4; byte++) {
        reply[byte] = value >> (byte * 8);
    }
}

static int screenshot_parse_word(const uint8_t *text, unsigned length, unsigned *value) {
    if (!length || length > 10) {
        return 0;
    }
    unsigned parsed = 0;
    for (unsigned i = 0; i < length; i++) {
        if (text[i] < '0' || text[i] > '9' || parsed > (UINT32_MAX - (text[i] - '0')) / 10) {
            return 0;
        }
        parsed = parsed * 10 + text[i] - '0';
    }
    *value = parsed;
    return 1;
}

unsigned screenshot_command(const uint8_t *command, unsigned length, uint8_t *reply) {
    if (command_equals(command, length, "SCREENSHOT CAPTURE")) {
        screenshot_begin();
        return reply_write_text(
            reply, copying ? "OK screenshot requested\n" : "ERR display unavailable\n");
    }
    if (command_equals(command, length, "SCREENSHOT STATUS")) {
        unsigned values[] = {
            ready, copying, screenshot_id, SCREENSHOT_BYTES, SCREENSHOT_CHUNK_BYTES};
        const char *names[] = {"SCREENSHOT ready=", " busy=", " id=", " bytes=", " chunk="};
        unsigned reply_length = 0;
        for (unsigned i = 0; i < 5; i++) {
            reply_length += reply_write_text(reply + reply_length, names[i]);
            reply_length += reply_write_hex32(reply + reply_length, values[i]);
        }
        reply[reply_length++] = '\n';
        return reply_length;
    }
    if (length <= 16 || !command_equals(command, 16, "SCREENSHOT READ ")) {
        return 0;
    }
    unsigned split = 16;
    while (split < length && command[split] != ' ') {
        split++;
    }
    unsigned requested_id, offset;
    if (split == length || !screenshot_parse_word(command + 16, split - 16, &requested_id) ||
        !screenshot_parse_word(command + split + 1, length - split - 1, &offset) || !ready ||
        copying || requested_id != screenshot_id || offset >= SCREENSHOT_BYTES) {
        return reply_write_text(reply, "ERR screenshot unavailable or stale\n");
    }
    unsigned count = SCREENSHOT_BYTES - offset;
    if (count > SCREENSHOT_CHUNK_BYTES) {
        count = SCREENSHOT_CHUNK_BYTES;
    }
    unsigned header[] = {0x31534353, screenshot_id, offset, count, SCREENSHOT_BYTES, 800, 480, 0};
    for (unsigned i = 0; i < 8; i++) {
        screenshot_write_word(reply + i * 4, header[i]);
    }
    const uint8_t *pixels = (const uint8_t *)SCREENSHOT_BUFFER;
    for (unsigned i = 0; i < count; i++) {
        reply[32 + i] = pixels[offset + i];
    }
    screenshot_write_word(reply + 28, scope_crc32(reply, 32 + count));
    return 32 + count;
}
