#include "front_panel.h"
#include "panel_tools.h"
#include "scope_internal.h"
#include "screenshot.h"
#include "spectrum.h"

#define PANEL_UI_CODE __attribute__((section(".panel_text"), noinline))
#define PANEL_SOFTKEY_FIRST_CENTER_Y 80u
#define PANEL_SOFTKEY_SPACING_Y 80u
#define PANEL_MENU_ITEMS_PER_PAGE 4u

struct menu_location {
    unsigned menu, selected_action;
};
static struct menu_location menu_history[4];
static unsigned menu_depth, menu_editing;
static unsigned current_menu, selected_action, menu_revision = 1;
static unsigned rendered_menu_revision[2];
static unsigned measurement_channel;

static void menu_format_unsigned(char *text, unsigned value) {
    char reversed[10];
    unsigned digit_count = 0, length = 0;
    do {
        reversed[digit_count++] = '0' + value % 10;
        value /= 10;
    } while (value);
    while (digit_count) {
        text[length++] = reversed[--digit_count];
    }
    text[length] = 0;
}

static PANEL_UI_CODE unsigned menu_item_count(void) {
    switch (current_menu) {
    case PANEL_MENU_CLOSED:
        return 0;
    case PANEL_MENU_AUTOSET:
        return 2;
    case PANEL_MENU_HORIZONTAL:
    case PANEL_MENU_DISPLAY:
    case PANEL_MENU_COPY:
    case PANEL_MENU_TRIGGER_DETAILS:
        return 4;
    default:
        return 5;
    }
}

static PANEL_UI_CODE unsigned menu_first_visible_action(void) {
    unsigned count = menu_item_count();
    if (!count) {
        return 0;
    }
    unsigned action = selected_action < count ? selected_action : count - 1;
    return action / PANEL_MENU_ITEMS_PER_PAGE * PANEL_MENU_ITEMS_PER_PAGE;
}

void front_panel_menu_refresh(void) {
    scope_controls_changed();
    scope_state.display.display_redraw_requested = 1;
}

void front_panel_menu_close(void) {
    if (current_menu) {
        menu_revision++;
    }
    current_menu = PANEL_MENU_CLOSED;
    selected_action = menu_editing = menu_depth = 0;
    front_panel_menu_refresh();
}

PANEL_UI_CODE void front_panel_menu_back(void) {
    if (menu_editing) {
        menu_editing = 0;
    } else if (menu_depth) {
        struct menu_location parent = menu_history[--menu_depth];
        current_menu = parent.menu;
        selected_action = parent.selected_action;
    } else {
        front_panel_menu_close();
        return;
    }
    front_panel_menu_refresh();
}

static PANEL_UI_CODE void menu_enter_submenu(unsigned menu) {
    if (menu_depth >= sizeof(menu_history) / sizeof(menu_history[0])) {
        return;
    }
    menu_history[menu_depth++] = (struct menu_location){current_menu, selected_action};
    current_menu = menu;
    selected_action = menu_editing = 0;
}

void front_panel_menu_toggle(unsigned menu) {
    unsigned previous_menu = current_menu;
    current_menu = current_menu == menu ? PANEL_MENU_CLOSED : menu;
    selected_action = menu_editing = menu_depth = 0;
    if (!!previous_menu != !!current_menu) {
        menu_revision++;
    }
    front_panel_menu_refresh();
}

PANEL_UI_CODE void front_panel_menu_state(unsigned *state) {
    state[0] = current_menu;
    state[1] = selected_action;
    state[2] = menu_editing;
    state[3] = current_menu ? menu_first_visible_action() / PANEL_MENU_ITEMS_PER_PAGE + 1 : 0;
}

PANEL_UI_CODE void front_panel_menu_rotate(int direction) {
    if (!current_menu) {
        return;
    }
    if (!menu_editing) {
        int selection = (int)selected_action + direction;
        unsigned count = menu_item_count();
        selected_action = selection < 0 ? 0 : (unsigned)selection > count ? count : selection;
    } else if (current_menu == PANEL_MENU_UTILITY && selected_action == 0) {
        int brightness = (int)display_brightness() + direction;
        front_panel_issue_value("DISPLAY BRIGHTNESS ",
                                brightness < 1     ? 1
                                : brightness > 100 ? 100
                                                   : brightness);
    } else if (current_menu == PANEL_MENU_CURSOR &&
               (selected_action == 2 || selected_action == 3)) {
        panel_cursor_move(direction);
    } else if (current_menu == PANEL_MENU_MATH &&
               (panel_math_mode() == 3 ? selected_action == 4
                                       : (selected_action == 1 || selected_action == 2))) {
        panel_math_adjust(selected_action, selected_action == 1 ? -direction : direction);
    } else if (current_menu == PANEL_MENU_TRIGGER_DETAILS && selected_action == 2) {
        int64_t holdoff_ns = scope_state.trigger.configured.holdoff_ns + (int64_t)direction * 100;
        front_panel_issue_value("TRIGGER HOLDOFF ",
                                holdoff_ns < 100          ? 100
                                : holdoff_ns > 1000000000 ? 1000000000
                                                          : holdoff_ns);
    }
    front_panel_menu_refresh();
}

static const char *channel_toggle_command(unsigned channel,
                                          const char *ch1_on,
                                          const char *ch1_off,
                                          const char *ch2_on,
                                          const char *ch2_off,
                                          unsigned enabled) {
    return channel ? (enabled ? ch2_off : ch2_on) : (enabled ? ch1_off : ch1_on);
}

static void channel_menu_action(unsigned channel, unsigned action) {
    const char *command = 0;
    if (action == 0) {
        command = channel_toggle_command(channel,
                                         "CH1 COUPLING AC",
                                         "CH1 COUPLING DC",
                                         "CH2 COUPLING AC",
                                         "CH2 COUPLING DC",
                                         scope_state.channels.coupling[channel]);
    } else if (action == 1) {
        unsigned probe = scope_state.channels.probe_multiplier[channel];
        command = channel ? (probe == 1     ? "CH2 PROBE 10"
                             : probe == 10  ? "CH2 PROBE 100"
                             : probe == 100 ? "CH2 PROBE 1000"
                                            : "CH2 PROBE 1")
                          : (probe == 1     ? "CH1 PROBE 10"
                             : probe == 10  ? "CH1 PROBE 100"
                             : probe == 100 ? "CH1 PROBE 1000"
                                            : "CH1 PROBE 1");
    } else if (action == 2) {
        command = channel_toggle_command(channel,
                                         "CH1 BANDWIDTH ON",
                                         "CH1 BANDWIDTH OFF",
                                         "CH2 BANDWIDTH ON",
                                         "CH2 BANDWIDTH OFF",
                                         scope_state.configuration.features.bandwidth[channel]);
    } else if (action == 3) {
        command = channel_toggle_command(channel,
                                         "CH1 INVERSE ON",
                                         "CH1 INVERSE OFF",
                                         "CH2 INVERSE ON",
                                         "CH2 INVERSE OFF",
                                         scope_state.channels.inverse[channel]);
    } else if (action == 4) {
        command = channel_toggle_command(channel,
                                         "CH1 DISPLAY ON",
                                         "CH1 DISPLAY OFF",
                                         "CH2 DISPLAY ON",
                                         "CH2 DISPLAY OFF",
                                         scope_state.channels.visible[channel]);
    }
    if (command) {
        front_panel_issue(command);
    }
}

static void trigger_menu_action(unsigned action) {
    if (action == 0) {
        front_panel_issue(scope_state.trigger.configured.source ? "TRIGGER SOURCE CH1"
                                                                : "TRIGGER SOURCE CH2");
    } else if (action == 1) {
        front_panel_issue(scope_state.trigger.configured.slope ? "TRIGGER SLOPE RISE"
                                                               : "TRIGGER SLOPE FALL");
    } else if (action == 2) {
        front_panel_issue(scope_state.trigger.configured.sweep == 0   ? "TRIGGER SWEEP NORMAL"
                          : scope_state.trigger.configured.sweep == 1 ? "TRIGGER SINGLE"
                                                                      : "LIVE RUN");
    } else if (action == 3) {
        front_panel_issue(scope_state.trigger.enabled ? "TRIGGER ENABLE OFF" : "TRIGGER ENABLE ON");
    } else if (action == 4) {
        menu_enter_submenu(PANEL_MENU_TRIGGER_DETAILS);
    }
}

static PANEL_UI_CODE void menu_perform_action(unsigned action) {
    if (!current_menu || action > 4) {
        return;
    }
    selected_action = action;
    if ((current_menu == PANEL_MENU_UTILITY && action == 0) ||
        (current_menu == PANEL_MENU_MATH &&
         (panel_math_mode() == 3 ? action == 4 : (action == 1 || action == 2))) ||
        (current_menu == PANEL_MENU_TRIGGER_DETAILS && action == 2)) {
        menu_editing = 1;
        front_panel_menu_refresh();
        return;
    }
    if (current_menu == PANEL_MENU_CURSOR && (action == 2 || action == 3)) {
        panel_cursor_action(action);
        menu_editing = 1;
        front_panel_menu_refresh();
        return;
    }
    unsigned previous_menu = current_menu;
    if (current_menu == PANEL_MENU_CH1 || current_menu == PANEL_MENU_CH2) {
        channel_menu_action(current_menu == PANEL_MENU_CH2, action);
    } else if (current_menu == PANEL_MENU_TRIGGER) {
        trigger_menu_action(action);
    } else if (current_menu == PANEL_MENU_TRIGGER_DETAILS) {
        if (action == 0) {
            unsigned mode = scope_state.trigger.advanced.mode;
            front_panel_issue(mode == 0   ? "TRIGGER MODE VIDEO"
                              : mode == 1 ? "TRIGGER MODE PULSE"
                                          : "TRIGGER MODE EDGE");
        } else if (action == 1) {
            front_panel_issue(scope_state.trigger.configured.coupling ? "TRIGGER COUPLING DC"
                                                                      : "TRIGGER COUPLING AC");
        } else if (action == 3) {
            front_panel_center_trigger();
        }
    } else if (current_menu == PANEL_MENU_ACQUIRE) {
        if (action == 0) {
            const char *modes[] = {
                "ACQUIRE MODE PEAK", "ACQUIRE MODE AVERAGE", "ACQUIRE MODE SAMPLE"};
            front_panel_issue(modes[scope_state.configuration.features.acquire_mode]);
        } else if (action == 1) {
            unsigned count = scope_state.configuration.features.average_count;
            front_panel_issue(count == 4    ? "ACQUIRE AVERAGES 16"
                              : count == 16 ? "ACQUIRE AVERAGES 64"
                              : count == 64 ? "ACQUIRE AVERAGES 128"
                                            : "ACQUIRE AVERAGES 4");
        } else if (action == 2) {
            front_panel_issue(scope_state.capture.roll_live ? "ROLL MODE TRIGGERED"
                                                            : "ROLL MODE LIVE");
        } else if (action == 3) {
            front_panel_issue("LIVE OFF");
            front_panel_issue(scope_state.configuration.acquisition.record_depth == 1000
                                  ? "RECORD DEPTH 10000"
                                  : "RECORD DEPTH 1000");
        } else {
            front_panel_issue("TRIGGER SINGLE");
        }
    } else if (current_menu == PANEL_MENU_HORIZONTAL) {
        if (action == 0) {
            front_panel_issue("HORIZONTAL STOCK");
        } else if (action == 1) {
            front_panel_issue(scope_state.capture.roll_live ? "ROLL MODE TRIGGERED"
                                                            : "ROLL MODE LIVE");
        } else if (action == 2) {
            menu_enter_submenu(PANEL_MENU_ACQUIRE);
        } else if (action == 3) {
            front_panel_issue("LIVE OFF");
            front_panel_issue("RECORD START BOTH");
        }
    } else if (current_menu == PANEL_MENU_DISPLAY) {
        if (action < 2) {
            channel_menu_action(action, 4);
        } else if (action < 4) {
            channel_menu_action(action - 2, 3);
        }
    } else if (current_menu == PANEL_MENU_UTILITY) {
        if (action == 4) {
            front_panel_issue(scope_state.display.light_mode ? "DISPLAY THEME DARK"
                                                             : "DISPLAY THEME LIGHT");
        } else if (action == 1) {
            menu_enter_submenu(PANEL_MENU_DISPLAY);
        } else if (action == 2) {
            unsigned running = scope_state.capture.live_enabled;
            front_panel_issue("LIVE OFF");
            front_panel_issue(running ? "SETTINGS SAVE RUN" : "SETTINGS SAVE");
            if (running) {
                front_panel_issue("LIVE ON");
            }
        } else if (action == 3) {
            front_panel_issue("LIVE OFF");
            front_panel_issue("SETTINGS LOAD");
        } else {
            current_menu = PANEL_MENU_CLOSED;
        }
    } else if (current_menu == PANEL_MENU_CURSOR) {
        panel_cursor_action(action);
    } else if (current_menu == PANEL_MENU_MATH) {
        panel_math_action(action);
    } else if (current_menu == PANEL_MENU_MEASURE) {
        if (action == 0) {
            measurement_channel ^= 1;
        }
    } else if (current_menu == PANEL_MENU_AUTOSET) {
        if (action == 0) {
            panel_autoset_start();
        } else if (action == 1) {
            panel_autoset_cancel();
        }
    } else if (current_menu == PANEL_MENU_COPY && action == 0) {
        screenshot_begin();
    }
    if (!!previous_menu != !!current_menu) {
        menu_revision++;
    }
    front_panel_menu_refresh();
}

PANEL_UI_CODE void front_panel_menu_activate(void) {
    if (!current_menu) {
        return;
    }
    if (menu_editing) {
        menu_editing = 0;
        front_panel_menu_refresh();
    } else if (selected_action == menu_item_count()) {
        front_panel_menu_back();
    } else {
        menu_perform_action(selected_action);
    }
}

PANEL_UI_CODE void front_panel_menu_action(unsigned softkey) {
    if (!current_menu || softkey > 4) {
        return;
    }
    if (softkey == 4) {
        front_panel_menu_back();
        return;
    }
    unsigned action = menu_first_visible_action() + softkey;
    if (action >= menu_item_count()) {
        return;
    }
    if (menu_editing && selected_action == action) {
        menu_editing = 0;
        front_panel_menu_refresh();
        return;
    }
    menu_editing = 0;
    menu_perform_action(action);
}

void front_panel_display_prepare(void) {
    unsigned buffer = scope_state.display.draw_buffer_index;
    if (rendered_menu_revision[buffer] != menu_revision) {
        scope_state.display.draw_buffer_valid = 0;
        rendered_menu_revision[buffer] = menu_revision;
    }
}

static void measurement_values(char text[5][28], const char **values) {
    struct scope_signal_summary signal = scope_summarize_channel(measurement_channel);
    unsigned range = scope_state.frame.words[8 + measurement_channel];
    if (!signal.count || range >= 14 ||
        scope_state.frame.calibration_ids[measurement_channel] !=
            scope_adc_calibration_id(measurement_channel, range)) {
        for (unsigned i = 1; i < 5; i++) {
            values[i] = "--";
        }
        return;
    }
    if (signal.period_samples) {
        unsigned rate = scope_state.frame.words[14];
        uint64_t denominator = (uint64_t)sample_rate_denominator(rate) * signal.period_samples;
        int64_t frequency_hz = scope_ratio(sample_rate_numerator(rate), denominator);
        menu_format_unsigned(text[1], frequency_hz);
        hud_format_time(
            text[2], scope_ratio((int64_t)denominator * 1000000000LL, sample_rate_numerator(rate)));
        values[1] = text[1];
        values[2] = text[2];
    } else {
        values[1] = values[2] = "--";
    }
    struct scope_voltage_cal calibration;
    scope_get_voltage_cal(measurement_channel, range, &calibration);
    unsigned offset = (scope_state.frame.words[13] >> (measurement_channel * 16)) & 65535;
    unsigned probe = scope_state.channels.probe_multiplier[measurement_channel];
    int64_t mean_uv = (scope_voltage_uv(&calibration, 0, offset) +
                       scope_ratio((int64_t)signal.mean_code_q8 * calibration.gain, 65536)) *
                      probe;
    int64_t ac_rms_uv =
        scope_ratio((int64_t)signal.ac_rms_code_q8 * calibration.gain * probe, 65536);
    hud_format_voltage(text[3], mean_uv);
    hud_format_voltage(text[4], ac_rms_uv);
    values[3] = text[3];
    values[4] = text[4];
}

__attribute__((section(".panel_text"), noinline)) void front_panel_display_draw(void) {
    if (!current_menu) {
        return;
    }
    volatile uint16_t *framebuffer = scope_state.display.draw_framebuffer;
    unsigned menu_right = DISPLAY_PLOT_LEFT + DISPLAY_PLOT_WIDTH;
    unsigned menu_left = menu_right - 125;
    display_fill_rectangle(
        menu_left, DISPLAY_PLOT_TOP, 125, DISPLAY_PLOT_HEIGHT, display_background_color());
    for (unsigned y = DISPLAY_PLOT_TOP; y < DISPLAY_PLOT_TOP + DISPLAY_PLOT_HEIGHT; y++) {
        framebuffer[y * 800 + menu_left] = 0x4208;
    }
    const char *title = "", *labels[5] = {"", "", "", "", "CLOSE"};
    const char *values[5] = {"", "", "", "", ""};
    char formatted_values[5][28] = {{0}};
    unsigned channel = current_menu == PANEL_MENU_CH2;
    if (current_menu == PANEL_MENU_CH1 || current_menu == PANEL_MENU_CH2) {
        title = channel ? "CHANNEL 2" : "CHANNEL 1";
        labels[0] = "COUPLING";
        values[0] = scope_state.channels.coupling[channel] ? "AC" : "DC";
        labels[1] = "PROBE";
        unsigned probe = scope_state.channels.probe_multiplier[channel];
        values[1] = probe == 1 ? "1X" : probe == 10 ? "10X" : probe == 100 ? "100X" : "1000X";
        labels[2] = "BANDWIDTH";
        values[2] = scope_state.configuration.features.bandwidth[channel] ? "20 MHZ" : "FULL";
        labels[3] = "INVERT";
        values[3] = scope_state.channels.inverse[channel] ? "ON" : "OFF";
        labels[4] = "CHANNEL";
        values[4] = scope_state.channels.visible[channel] ? "ON" : "OFF";
    } else if (current_menu == PANEL_MENU_TRIGGER) {
        title = "TRIGGER";
        labels[0] = "SOURCE";
        values[0] = scope_state.trigger.configured.source ? "CH2" : "CH1";
        labels[1] = "EDGE";
        values[1] = scope_state.trigger.configured.slope ? "FALL" : "RISE";
        labels[2] = "SWEEP";
        values[2] = scope_state.trigger.configured.sweep == 0   ? "AUTO"
                    : scope_state.trigger.configured.sweep == 1 ? "NORMAL"
                                                                : "SINGLE";
        labels[3] = "TRIGGER";
        values[3] = scope_state.trigger.enabled ? "ON" : "FREE RUN";
        labels[4] = "MORE";
    } else if (current_menu == PANEL_MENU_TRIGGER_DETAILS) {
        title = "TRIGGER / MORE";
        labels[0] = "MODE";
        const char *modes[] = {
            "EDGE", "VIDEO", "PULSE", "SLOPE", "RUNT", "SPIKE", "TIMEOUT", "NTH EDGE"};
        values[0] = modes[scope_state.trigger.advanced.mode];
        labels[1] = "COUPLING";
        values[1] = scope_state.trigger.configured.coupling ? "AC" : "DC";
        labels[2] = "HOLDOFF";
        hud_format_time(formatted_values[2], scope_state.trigger.configured.holdoff_ns);
        values[2] = formatted_values[2];
        labels[3] = "LEVEL 50%";
        labels[4] = "BACK";
    } else if (current_menu == PANEL_MENU_ACQUIRE) {
        title = "ACQUISITION";
        labels[0] = "MODE";
        const char *modes[] = {"SAMPLE", "PEAK", "AVERAGE"};
        values[0] = modes[scope_state.configuration.features.acquire_mode];
        labels[1] = "AVERAGES";
        unsigned count = scope_state.configuration.features.average_count;
        values[1] = count == 4 ? "4" : count == 16 ? "16" : count == 64 ? "64" : "128";
        labels[2] = "ROLL";
        values[2] = scope_state.capture.roll_live ? "LIVE" : "TRIGGERED";
        labels[3] = "DEPTH (STOPS)";
        values[3] = scope_state.configuration.acquisition.record_depth == 1000 ? "1K" : "10K";
        labels[4] = "SINGLE CAPTURE";
    } else if (current_menu == PANEL_MENU_HORIZONTAL) {
        title = "HORIZONTAL";
        labels[0] = "CENTER POSITION";
        labels[1] = "ROLL";
        values[1] = scope_state.capture.roll_live ? "LIVE" : "TRIGGERED";
        labels[2] = "ACQUISITION";
        labels[3] = "CAPTURE RECORD";
        values[3] = "BOTH CHANNELS";
    } else if (current_menu == PANEL_MENU_DISPLAY) {
        title = "DISPLAY";
        labels[0] = "CH1";
        values[0] = scope_state.channels.visible[0] ? "ON" : "OFF";
        labels[1] = "CH2";
        values[1] = scope_state.channels.visible[1] ? "ON" : "OFF";
        labels[2] = "CH1 INVERT";
        values[2] = scope_state.channels.inverse[0] ? "ON" : "OFF";
        labels[3] = "CH2 INVERT";
        values[3] = scope_state.channels.inverse[1] ? "ON" : "OFF";
    } else if (current_menu == PANEL_MENU_UTILITY) {
        title = "UTILITY";
        labels[0] = "BACKLIGHT (%)";
        menu_format_unsigned(formatted_values[0], display_brightness());
        values[0] = formatted_values[0];
        labels[1] = "DISPLAY";
        labels[2] = "SAVE SETTINGS";
        values[2] = scope_state.settings.save_error ? "SAVE FAILED"
                    : scope_state.settings.dirty    ? "CHANGES PENDING"
                    : scope_settings_sequence()     ? "SAVED"
                                                    : "NO RECORD";
        labels[3] = "LOAD SETTINGS";
        values[3] = scope_state.settings.restore_state == 1 ? "LOADING"
                    : scope_settings_sequence()             ? "RECORD FOUND"
                                                            : "NO RECORD";
        labels[4] = "THEME";
        values[4] = scope_state.display.light_mode ? "LIGHT" : "DARK";
    } else if (current_menu == PANEL_MENU_MEASURE) {
        title = "MEASUREMENTS";
        labels[0] = "SOURCE";
        values[0] = measurement_channel ? "CH2" : "CH1";
        labels[1] = "FREQUENCY (HZ)";
        labels[2] = "PERIOD";
        labels[3] = "MEAN";
        labels[4] = "AC RMS";
        measurement_values(formatted_values, values);
    } else if (current_menu == PANEL_MENU_CURSOR) {
        title = "CURSORS";
        labels[0] = "TYPE";
        values[0] = panel_cursor_mode() == 0   ? "OFF"
                    : panel_cursor_mode() == 1 ? "TIME"
                                               : "VOLTAGE";
        labels[1] = "SOURCE";
        values[1] = panel_cursor_source() ? "CH2" : "CH1";
        labels[2] = "CURSOR A";
        values[2] = panel_cursor_selected() == 0 ? "SELECTED" : "";
        labels[3] = "CURSOR B";
        values[3] = panel_cursor_selected() == 1 ? "SELECTED" : "";
        labels[4] = "CENTER CURSORS";
    } else if (current_menu == PANEL_MENU_MATH) {
        title = "MATH";
        labels[0] = "OPERATION";
        static const char *const operations[] = {
            "OFF", "ADD", "SUBTRACT", "FFT", "MULTIPLY", "DIVIDE", "XY"};
        values[0] = operations[panel_math_mode()];
        labels[1] = "SCALE";
        panel_math_format_value(formatted_values[1], panel_math_scale_uv());
        values[1] = formatted_values[1];
        labels[2] = "POSITION";
        panel_math_format_value(
            formatted_values[2],
            scope_ratio((int64_t)panel_math_position() * panel_math_scale_uv(), 1000));
        values[2] = formatted_values[2];
        labels[3] = "ORDER";
        values[3] = panel_math_reversed() ? "CH2 - CH1" : "CH1 - CH2";
        if (panel_math_mode() != 2) {
            labels[3] = "INPUTS";
            values[3] = panel_math_mode() == 6 ? "X:CH1 Y:CH2" : "CH1 AND CH2";
        }
        labels[4] = "FIT TO CH1";
        if (panel_math_mode() == 3) {
            labels[1] = "SOURCE";
            values[1] = panel_fft_source() ? "CH2" : "CH1";
            labels[2] = "WINDOW";
            values[2] = panel_fft_window_name();
            labels[3] = "UNITS";
            values[3] = panel_fft_logarithmic() ? "DBV" : "VRMS";
            labels[4] = "SCALE";
            if (panel_fft_logarithmic()) {
                menu_format_unsigned(formatted_values[4], panel_fft_db_per_division());
                values[4] = formatted_values[4];
            } else {
                hud_format_voltage(formatted_values[4], panel_math_scale_uv());
                values[4] = formatted_values[4];
            }
        }

    } else if (current_menu == PANEL_MENU_AUTOSET) {
        title = "AUTOSET";
        const char *states[] = {"IDLE",
                                "CENTERING",
                                "SCALING",
                                "FINDING EDGE",
                                "COMPLETE",
                                "NO CLEAR SIGNAL",
                                "NO STABLE EDGE",
                                "UNAVAILABLE",
                                "CANCELLED",
                                "CHECKING SIGNAL",
                                "INPUT CLIPPED"};
        labels[0] = "START";
        values[0] = states[panel_autoset_state()];
        labels[1] = "CANCEL";
        labels[2] = "SIGNAL SEARCH";
        values[2] = "CURRENT RECORD";
    } else if (current_menu == PANEL_MENU_COPY) {
        title = "COPY SCREEN";
        labels[0] = "CAPTURE";
        values[0] = screenshot_busy() ? "COPYING" : screenshot_ready() ? "SAVED IN RAM" : "NO COPY";
        labels[1] = "DOWNLOAD";
        values[1] = "PC VIA USB";
        labels[2] = "RESOLUTION";
        values[2] = "800 X 480";
        labels[3] = "USB DRIVE";
        values[3] = "NOT AVAILABLE";
    }
    hud_text(menu_left + 8, 46, title, 0xffff);
    unsigned count = menu_item_count();
    unsigned first_action = menu_first_visible_action();
    if (count > PANEL_MENU_ITEMS_PER_PAGE) {
        char page_text[] = {'1' + first_action / PANEL_MENU_ITEMS_PER_PAGE,
                            '/',
                            '1' + (count - 1) / PANEL_MENU_ITEMS_PER_PAGE,
                            0};
        hud_text(748, 57, page_text, 0x7bef);
    }
    for (unsigned row = 0; row < 5; row++) {
        unsigned action = first_action + row;
        unsigned back = row == 4;
        if (!back && action >= count) {
            continue;
        }
        unsigned selected = selected_action == (back ? count : action);
        unsigned color = selected ? (menu_editing ? 0xffe0 : 0x07e0) : 0xffff;
        const char *label = back ? "BACK" : labels[action];
        const char *value = back ? (menu_editing ? "TO MENU"
                                    : menu_depth ? "PREVIOUS MENU"
                                                 : "CLOSE MENU")
                                 : values[action];
        unsigned center_y = PANEL_SOFTKEY_FIRST_CENTER_Y + row * PANEL_SOFTKEY_SPACING_Y;
        hud_text(menu_left + 8, center_y - 10, label, color);
        hud_text(menu_left + 8, center_y + 3, value, selected && menu_editing ? color : 0x7bef);
        for (unsigned x = menu_right - 6; x < menu_right; x++) {
            framebuffer[center_y * 800 + x] = display_foreground_color(color);
        }
    }
    hud_text(menu_left + 8,
             420,
             front_panel_command_failed() ? "CONTROL REJECTED"
             : menu_editing               ? "TURN: ADJUST"
                                          : "TURN: MOVE",
             front_panel_command_failed() ? 0xf800 : 0x7bef);
    hud_text(menu_left + 8, 432, menu_editing ? "PRESS: DONE" : "PRESS: SELECT", 0x7bef);
}
