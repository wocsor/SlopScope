/* Normal trigger-family register plans recovered from stock ebfc0..ec310 */
#include "advanced.h"
#include "timer_rounding.h"
void scope_advanced_defaults(struct scope_advanced_trigger *advanced_trigger) {
    *advanced_trigger = (struct scope_advanced_trigger){0, 0, 500000, 1000000, 3000000, 2, 1, 0, 0};
}
unsigned scope_video_line_max(unsigned standard) {
    return standard == 0 ? 525 : standard < 3 ? 625 : 0;
}

void scope_video_normalize(struct scope_advanced_trigger *advanced_trigger) {
    if (advanced_trigger->mode != 1) {
        return;
    }
    if (advanced_trigger->video_sync > 4) {
        advanced_trigger->video_sync = 0;
    }
    if (advanced_trigger->video_sync == 4) {
        unsigned maximum_line = scope_video_line_max(advanced_trigger->video_standard);
        if (!advanced_trigger->video_count) {
            advanced_trigger->video_count = 1;
        }
        if (maximum_line && advanced_trigger->video_count > maximum_line) {
            advanced_trigger->video_count = maximum_line;
        }
    }
}
int scope_advanced_valid(const struct scope_advanced_trigger *advanced_trigger) {
    return advanced_trigger && advanced_trigger->mode < 8 && advanced_trigger->condition < 6 &&
           advanced_trigger->width_ns >= 100 && advanced_trigger->width_ns <= 1000000000 &&
           advanced_trigger->lower_uv >= -1000000000 && advanced_trigger->upper_uv <= 1000000000 &&
           advanced_trigger->lower_uv < advanced_trigger->upper_uv &&
           advanced_trigger->count >= 1 && advanced_trigger->count <= 65535 &&
           advanced_trigger->video_count <= 65535 && advanced_trigger->video_sync < 64 &&
           advanced_trigger->video_standard < 3;
}
unsigned scope_timer_word(unsigned cycles, unsigned rounding) {
    unsigned exponent = 0;
    while (cycles >= 1000) {
        cycles /= 10;
        exponent++;
    }
    unsigned mantissa = rounding == 0   ? timer_upper[cycles]
                        : rounding == 2 ? timer_lower[cycles]
                                        : cycles;
    return (mantissa << 6) | exponent;
}
int scope_advanced_plan(const struct scope_advanced_trigger *advanced_trigger,
                        unsigned channel_index,
                        unsigned slope,
                        unsigned sweep,
                        unsigned inverse,
                        unsigned holdoff_ns,
                        int lower_threshold,
                        int upper_threshold,
                        struct scope_trigger_write *out,
                        unsigned *count) {
    if (count) {
        *count = 0;
    }
    if (!scope_advanced_valid(advanced_trigger) || !out || !count || !advanced_trigger->mode ||
        channel_index > 1 || slope > 1 || sweep > 2 || inverse > 1 || holdoff_ns < 100 ||
        holdoff_ns > 1000000000) {
        return 0;
    }
    struct scope_advanced_trigger video;
    if (advanced_trigger->mode == 1) {
        video = *advanced_trigger;
        scope_video_normalize(&video);
        advanced_trigger = &video;
        sweep = 0;
    }
    unsigned mode = advanced_trigger->mode, condition = advanced_trigger->condition;
    if (inverse) {
        condition = condition < 3 ? condition + 3 : condition - 3;
    }
    unsigned encoded_condition = condition > 2 ? condition + 1 : condition;
    unsigned flag = slope ^ inverse, mode_bits = encoded_condition;
    if (mode == 1) {
        mode_bits = advanced_trigger->video_standard | (advanced_trigger->video_sync << 2);
    }
    if (mode == 2) {
        flag = (advanced_trigger->condition < 3) ^ inverse;
    }
    if (mode == 3) {
        flag = (advanced_trigger->condition >= 3) ^ inverse;
    }
    if (mode == 5) {
        mode_bits = condition + (slope ? 4 : 0);
    }
    if (mode == 6) {
        mode_bits = (slope ^ inverse) ? 6 : 2;
    }
    if (mode == 7) {
        mode_bits = slope ^ inverse;
    }
    unsigned write_count = 0;
    out[write_count++] = (struct scope_trigger_write){
        0x300,
        0x200000u | (flag << 20) | (channel_index << 14) | (sweep << 12) | (mode << 8) | mode_bits};
    static const unsigned timing[2][3] = {{0x31c, 0x32c, 0x320}, {0x324, 0x330, 0x328}};
    unsigned cycles = mode == 1 ? advanced_trigger->video_count : advanced_trigger->width_ns / 8;
    for (unsigned i = 0; i < 3; i++) {
        out[write_count++] = (struct scope_trigger_write){
            timing[channel_index][i], scope_timer_word(cycles, mode == 1 ? 1 : i)};
    }
    if (mode >= 3 && mode <= 5) {
        out[write_count++] =
            (struct scope_trigger_write){channel_index ? 0x314 : 0x30c, (uint32_t)lower_threshold};
        out[write_count++] =
            (struct scope_trigger_write){channel_index ? 0x318 : 0x310, (uint32_t)upper_threshold};
    }
    if (mode == 7) {
        out[write_count++] =
            (struct scope_trigger_write){channel_index ? 0x338 : 0x334, advanced_trigger->count};
    }
    out[write_count++] = (struct scope_trigger_write){
        channel_index ? 0x308 : 0x304, scope_timer_word((mode == 1 ? 100 : holdoff_ns) / 8, 1)};
    *count = write_count;
    return 1;
}
