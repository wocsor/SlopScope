#ifndef SCOPE_PANEL_TOOLS_H
#define SCOPE_PANEL_TOOLS_H
#include <stdint.h>
struct scope_signal_summary {
    unsigned amplitude, period_samples, clipped, count;
    int minimum_code, maximum_code, mean_code_q8;
    unsigned ac_rms_code_q8;
};
struct scope_signal_summary scope_summarize_channel(unsigned channel_index);
unsigned panel_cursor_mode(void);
unsigned panel_cursor_source(void);
unsigned panel_cursor_selected(void);
void panel_cursor_action(unsigned action);
void panel_cursor_move(int steps);
unsigned panel_math_mode(void);
void panel_math_select_fft(unsigned range);
unsigned panel_math_range(void);
unsigned panel_math_reversed(void);
int panel_math_position(void);
uint64_t panel_math_scale_uv(void);
void panel_math_format_value(char *text, int64_t value_micro);
void panel_math_action(unsigned action);
void panel_math_adjust(unsigned action, int steps);
void panel_tools_erase(void);
void panel_tools_draw(void);
void panel_tools_readouts(void);
unsigned panel_tools_command(const uint8_t *command, unsigned length, uint8_t *reply);
#endif
