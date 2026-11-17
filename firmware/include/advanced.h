#ifndef SCOPE_ADVANCED_H
#define SCOPE_ADVANCED_H
#include <stdint.h>
struct scope_advanced_trigger {
    uint32_t mode, condition, width_ns;
    int32_t lower_uv, upper_uv;
    uint32_t count, video_count, video_sync, video_standard;
};
struct scope_trigger_write {
    uint32_t address, value;
};
void scope_advanced_defaults(struct scope_advanced_trigger *advanced_trigger);
unsigned scope_video_line_max(unsigned standard);
void scope_video_normalize(struct scope_advanced_trigger *advanced_trigger);
int scope_advanced_valid(const struct scope_advanced_trigger *advanced_trigger);
unsigned scope_timer_word(unsigned cycles, unsigned rounding);
int scope_advanced_plan(const struct scope_advanced_trigger *advanced_trigger,
                        unsigned channel_index,
                        unsigned slope,
                        unsigned sweep,
                        unsigned inverse,
                        unsigned holdoff_ns,
                        int lower_threshold,
                        int upper_threshold,
                        struct scope_trigger_write *out,
                        unsigned *count);
#endif
