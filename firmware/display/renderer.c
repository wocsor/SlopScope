#include "scope_internal.h"

static uint32_t activity_last_tick;
static uint32_t display_started;
static unsigned overlay_revision = 1;
static unsigned plot_revision = 1;
static unsigned rendered_overlay_revision[2];
static unsigned rendered_plot_revision[2];
static unsigned rendered_sequence[2];
static int16_t plot_y[2][2][750];
static int16_t voltage_y[2][256];
static unsigned int voltage_keys[2][9];

static unsigned display_grid_color_at(unsigned x, unsigned y);
static void
display_draw_trace_segment(unsigned x, int start_y, int end_y, unsigned color, int erase);
static void display_draw_waveforms(void);

static unsigned display_grid_color_at(unsigned x, unsigned y) {
    if (x < DISPLAY_PLOT_LEFT || x > DISPLAY_PLOT_LEFT + DISPLAY_PLOT_WIDTH ||
        y < DISPLAY_PLOT_TOP || y > DISPLAY_PLOT_TOP + DISPLAY_PLOT_HEIGHT) {
        return 0;
    }
    return (x == DISPLAY_PLOT_LEFT || x == DISPLAY_PLOT_LEFT + DISPLAY_PLOT_WIDTH ||
            (x - DISPLAY_PLOT_LEFT + 25) % 50 == 0 || (y - DISPLAY_PLOT_TOP) % 50 == 0)
               ? 0x2104
               : 0;
}

static void
display_draw_trace_segment(unsigned x, int start_y, int end_y, unsigned color, int erase) {
    volatile uint16_t *framebuffer = scope_state.display.draw_framebuffer;
    if (start_y > end_y) {
        int swap_y = start_y;
        start_y = end_y;
        end_y = swap_y;
    }
    if (start_y < DISPLAY_PLOT_TOP) {
        start_y = DISPLAY_PLOT_TOP;
    }
    if (end_y >= DISPLAY_PLOT_TOP + DISPLAY_PLOT_HEIGHT) {
        end_y = DISPLAY_PLOT_TOP + DISPLAY_PLOT_HEIGHT - 1;
    }
    for (int y = start_y; y <= end_y; y++) {
        unsigned previous_color = framebuffer[y * 800 + x];
        framebuffer[y * 800 + x] = erase ? display_grid_color_at(x, y)
                                   : ((previous_color == 0xffe0 && color == 0x07ff) ||
                                      (previous_color == 0x07ff && color == 0xffe0))
                                       ? 0xffff
                                       : color;
    }
}

static void display_draw_waveforms(void) {
    if (!scope_state.display.screen_on) {
        return;
    }
    volatile uint16_t *framebuffer = scope_state.display.draw_framebuffer;
    if (!scope_state.display.draw_buffer_valid) {
        for (unsigned y = 0; y < 480; y++) {
            for (unsigned x = 0; x < 800; x++) {
                framebuffer[y * 800 + x] = display_grid_color_at(x, y);
            }
            if (!(y & 3)) {
                usb_service();
            }
        }
    }
    if (scope_state.display.draw_buffer_valid) {
        for (unsigned channel_index = 0; channel_index < 2; channel_index++) {
            for (unsigned x = 0; x < DISPLAY_PLOT_WIDTH; x++) {
                if (!(x & 31)) {
                    usb_service();
                }
                if (plot_y[scope_state.display.draw_buffer_index][channel_index][x] != -32768) {
                    display_draw_trace_segment(
                        DISPLAY_PLOT_LEFT + x,
                        plot_y[scope_state.display.draw_buffer_index][channel_index][x],
                        x && plot_y[scope_state.display.draw_buffer_index][channel_index][x - 1] !=
                                    -32768
                            ? plot_y[scope_state.display.draw_buffer_index][channel_index][x - 1]
                            : plot_y[scope_state.display.draw_buffer_index][channel_index][x],
                        0,
                        1);
                }
            }
        }
    }
    for (unsigned channel_index = 0; channel_index < 2; channel_index++) {
        unsigned range = scope_state.frame.words[0] == 0x31435348
                             ? scope_state.frame.words[8 + channel_index]
                             : scope_state.channels.range[channel_index];
        unsigned offset_dac = scope_state.frame.words[0] == 0x31435348
                                  ? (scope_state.frame.words[13] >> (channel_index * 16)) & 65535
                                  : scope_state.channels.offset_dac[channel_index];
        if (scope_state.frame.calibration_ids[channel_index] !=
            scope_adc_calibration_id(channel_index, range)) {
            for (unsigned x = 0; x < DISPLAY_PLOT_WIDTH; x++) {
                plot_y[scope_state.display.draw_buffer_index][channel_index][x] = -32768;
            }
            continue;
        }
        struct scope_voltage_cal voltage_calibration;
        scope_get_voltage_cal(channel_index, range, &voltage_calibration);
        unsigned keys[9] = {(unsigned)voltage_calibration.zero,
                            voltage_calibration.gain,
                            voltage_calibration.dac,
                            (unsigned)voltage_calibration.slope,
                            voltage_calibration.valid,
                            offset_dac,
                            scope_state.channels.range[channel_index],
                            (unsigned)scope_state.channels.position_millidivisions[channel_index],
                            scope_state.channels.inverse[channel_index]},
                 changed = !scope_state.display.draw_buffer_valid;
        for (unsigned i = 0; i < 9; i++) {
            if (keys[i] != voltage_keys[channel_index][i]) {
                changed = 1;
            }
        }
        if (changed) {
            struct scope_plot_config plot_config;
            scope_plot_configure(&voltage_calibration,
                                 offset_dac,
                                 scope_state.channels.range[channel_index],
                                 scope_state.channels.position_millidivisions[channel_index],
                                 scope_state.channels.inverse[channel_index],
                                 &plot_config);
            for (unsigned code = 0; code < 256; code++) {
                if (!(code & 15)) {
                    usb_service();
                }
                voltage_y[channel_index][code] = scope_plot_y(&plot_config, (int8_t)code);
            }
            for (unsigned i = 0; i < 9; i++) {
                voltage_keys[channel_index][i] = keys[i];
            }
        }
        for (unsigned x = 0; x < DISPLAY_PLOT_WIDTH; x++) {
            unsigned index = (scope_state.frame.is_roll ? 1998 - scope_state.display.view_points
                                                        : scope_state.display.view_start) +
                             x * scope_state.display.view_points / DISPLAY_PLOT_WIDTH;
            if (index < scope_state.frame.valid_start ||
                index >= scope_state.frame.valid_start + scope_state.frame.valid_count) {
                plot_y[scope_state.display.draw_buffer_index][channel_index][x] = -32768;
                continue;
            }
            unsigned code =
                ((const uint8_t *)(scope_state.frame.words +
                                   16))[channel_index * 2000 + index +
                                        (channel_index ? scope_state.frame.channel_alignment : 0)];
            plot_y[scope_state.display.draw_buffer_index][channel_index][x] =
                voltage_y[channel_index][code];
        }
        if (scope_state.channels.visible[channel_index]) {
            for (unsigned x = 0; x < DISPLAY_PLOT_WIDTH; x++) {
                if (!(x & 31)) {
                    usb_service();
                }
                if (plot_y[scope_state.display.draw_buffer_index][channel_index][x] != -32768) {
                    display_draw_trace_segment(
                        DISPLAY_PLOT_LEFT + x,
                        plot_y[scope_state.display.draw_buffer_index][channel_index][x],
                        x && plot_y[scope_state.display.draw_buffer_index][channel_index][x - 1] !=
                                    -32768
                            ? plot_y[scope_state.display.draw_buffer_index][channel_index][x - 1]
                            : plot_y[scope_state.display.draw_buffer_index][channel_index][x],
                        channel_index ? 0x07ff : 0xffe0,
                        0);
                }
            }
        }
    }
    display_annotations();
    hud_draw_measurements();
    scope_state.display.buffer_ready[scope_state.display.draw_buffer_index] =
        scope_state.display.draw_buffer_valid = 1;
    scope_state.display.display_redraw_requested = 0;
}

void display_usb_activity_poll(void) {
    static uint32_t last_draw_ticks;
    if (!scope_state.display.screen_on || scope_state.boot.console_active ||
        !timer_has_elapsed(last_draw_ticks, 50)) {
        return;
    }
    last_draw_ticks = timer_ticks();
    static unsigned previous_activity = ~0u;
    unsigned activity = usb_activity_flags();
    if (activity != previous_activity) {
        previous_activity = activity;
        overlay_revision++;
    }
}

void activity_poll(void) {
    if (!scope_state.display.screen_on || scope_state.boot.console_active ||
        !timer_has_elapsed(activity_last_tick, 200)) {
        return;
    }
    activity_last_tick = timer_ticks();
    scope_state.display.activity_heartbeat++;
    overlay_revision++;
}

static void display_draw_activity(void) {
    for (unsigned y = 28; y < 36; y++) {
        for (unsigned x = 25; x < 775; x++) {
            scope_state.display.draw_framebuffer[y * 800 + x] = 0;
        }
    }
    unsigned x = hud_text(25,
                          28,
                          scope_state.capture.live_enabled || scope_state.record.active
                              ? ((scope_state.display.activity_heartbeat / 2) & 1 ? ". " : "  ")
                              : "  ",
                          0xffff);
    if (scope_state.capture.error) {
        hud_text(x, 28, "ACQUISITION ERROR", 0xf800);
    } else if (!scope_state.capture.live_enabled && !scope_state.record.active) {
        hud_text(x, 28, "STOPPED", 0xf800);
    } else {
        hud_text(x, 28, "ACQUIRING", 0xffff);
    }
}

void display_poll(void) {
    if (!scope_state.display.screen_on || !scope_state.frame.sequence ||
        (scope_state.boot.console_active && scope_state.boot.stage != BOOT_ACQUIRING)) {
        return;
    }
    scope_state.boot.console_active = 0;
    if (scope_state.display.display_redraw_requested) {
        plot_revision++;
        scope_state.display.display_redraw_requested = 0;
    }
    unsigned displayed = scope_state.display.displayed_buffer_index;
    if (scope_state.display.buffer_ready[displayed] &&
        rendered_sequence[displayed] == scope_state.frame.sequence &&
        rendered_plot_revision[displayed] == plot_revision &&
        rendered_overlay_revision[displayed] == overlay_revision) {
        return;
    }
    if (display_started - timer_ticks() < 800000u || display_present_pending()) {
        return; /* Present at most 30 fps; never reuse a buffer awaiting the LCD latch. */
    }
    display_started = timer_ticks();
    scope_state.display.draw_buffer_index = scope_state.display.displayed_buffer_index ^ 1u;
    scope_state.display.draw_framebuffer =
        (volatile uint16_t *)(uintptr_t)(scope_state.display.draw_buffer_index
                                             ? DISPLAY_FRAMEBUFFER_1
                                             : DISPLAY_FRAMEBUFFER_0);
    scope_state.display.draw_buffer_valid =
        scope_state.display.buffer_ready[scope_state.display.draw_buffer_index];
    unsigned drawing = scope_state.display.draw_buffer_index;
    if (!scope_state.display.draw_buffer_valid ||
        rendered_sequence[drawing] != scope_state.frame.sequence ||
        rendered_plot_revision[drawing] != plot_revision) {
        scope_state.display.display_redraw_requested =
            rendered_plot_revision[drawing] != plot_revision;
        display_draw_waveforms();
        rendered_sequence[drawing] = scope_state.frame.sequence;
        rendered_plot_revision[drawing] = plot_revision;
    }
    hud_draw_usb_status();
    display_draw_activity();
    rendered_overlay_revision[drawing] = overlay_revision;
    display_present((uintptr_t)scope_state.display.draw_framebuffer);
    scope_state.display.displayed_buffer_index = drawing;
    scope_state.display.display_last_ticks = display_started - timer_ticks();
    scope_state.display.display_sequence = scope_state.frame.sequence;
    scope_state.display.display_frames++;
}
