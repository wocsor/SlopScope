#include "scope_internal.h"

static unsigned int boot_lines;
static unsigned int boot_cursor_x;
static unsigned int boot_cursor_y;
static unsigned int boot_cursor_visible;
static uint32_t boot_cursor_started;

static void boot_cursor_draw(unsigned visible);

static int boot_frame_begin(void) {
    uint32_t started_ticks = timer_ticks();
    while (display_present_pending()) {
        usb_service();
        if (timer_has_elapsed(started_ticks, 50)) {
            return 0;
        }
    }
    unsigned displayed = scope_state.display.displayed_buffer_index;
    scope_state.display.draw_buffer_index = displayed ^ 1u;
    const volatile uint32_t *source =
        (const volatile uint32_t *)(uintptr_t)(displayed ? DISPLAY_FRAMEBUFFER_1
                                                         : DISPLAY_FRAMEBUFFER_0);
    volatile uint32_t *destination =
        (volatile uint32_t *)(uintptr_t)(displayed ? DISPLAY_FRAMEBUFFER_0 : DISPLAY_FRAMEBUFFER_1);
    for (unsigned word = 0; word < 800u * 480u / 2; word++) {
        destination[word] = source[word];
        if (!(word & 4095)) {
            usb_service();
        }
    }
    scope_state.display.draw_framebuffer = (volatile uint16_t *)destination;
    return 1;
}

static void boot_frame_present(void) {
    display_present((uintptr_t)scope_state.display.draw_framebuffer);
    scope_state.display.displayed_buffer_index = scope_state.display.draw_buffer_index;
}

static void boot_cursor_draw(unsigned visible) {
    volatile uint16_t *framebuffer = scope_state.display.draw_framebuffer;
    for (unsigned y = 0; y < 8; y++) {
        for (unsigned x = 0; x < 5; x++) {
            framebuffer[(boot_cursor_y + y) * 800 + boot_cursor_x + x] = visible ? 0xffff : 0;
        }
    }
    boot_cursor_visible = visible;
}

void scope_boot_cursor_poll(void) {
    if (!scope_state.boot.console_active || !boot_lines) {
        return;
    }
    if (timer_has_elapsed(boot_cursor_started, 350) && boot_frame_begin()) {
        boot_cursor_started = timer_ticks();
        boot_cursor_draw(!boot_cursor_visible);
        boot_frame_present();
    }
}

void scope_boot_log(const char *message) {
    if (!scope_state.boot.console_active || !boot_frame_begin()) {
        return;
    }
    if (boot_lines) {
        boot_cursor_draw(0);
    }
    volatile uint16_t *framebuffer = scope_state.display.draw_framebuffer;
    for (unsigned y = 0; y < BOOT_LOGO_HEIGHT; y++) {
        for (unsigned x = 0; x < BOOT_LOGO_WIDTH; x++) {
            framebuffer[(240 - BOOT_LOGO_HEIGHT / 2 + y) * 800 + 400 - BOOT_LOGO_WIDTH / 2 + x] =
                boot_logo_palette[boot_logo_pixels[y * BOOT_LOGO_WIDTH + x]];
        }
    }
    scope_state.display.draw_framebuffer = framebuffer;
    if (boot_lines >= 38) {
        for (unsigned y = 8; y < 452; y++) {
            for (unsigned x = 8; x < 300; x++) {
                framebuffer[y * 800 + x] = framebuffer[(y + 12) * 800 + x];
            }
        }
        for (unsigned y = 452; y < 472; y++) {
            for (unsigned x = 8; x < 300; x++) {
                framebuffer[y * 800 + x] = 0;
            }
        }
        boot_lines = 37;
    }
    hud_text(8, 8 + boot_lines * 12, "> ", 0x07e0);
    boot_cursor_x = hud_text(20, 8 + boot_lines * 12, message, 0xffff) + 6;
    boot_cursor_y = 8 + boot_lines * 12;
    boot_lines++;
    boot_cursor_started = timer_ticks();
    boot_cursor_draw(1);
    boot_frame_present();
}

void boot_screen_start(void) {
    scope_state.display.displayed_buffer_index = 0;
    scope_state.display.draw_framebuffer = (volatile uint16_t *)DISPLAY_FRAMEBUFFER_0;
    volatile uint16_t *framebuffer = scope_state.display.draw_framebuffer;
    for (unsigned i = 0; i < 800u * 480u; i++) {
        framebuffer[i] = 0;
    }
    display_prepare_from_fel();
    display_init_800x480(DISPLAY_FRAMEBUFFER_0);
    display_backlight(1);
    scope_state.boot.console_active = 1;
    boot_lines = 0;
    scope_boot_log("STARTING HARDWARE");
}
