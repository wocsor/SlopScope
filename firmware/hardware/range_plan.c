/* DOS1102 model 0x069194c5: pure range-switch write plan */
#include "range_plan.h"
struct range_entry {
    unsigned bank, bits;
};
static const struct range_entry ranges[14] = {{0, 3},
                                              {0, 3},
                                              {0, 3},
                                              {0, 3},
                                              {0, 0},
                                              {0, 2},
                                              {0, 6},
                                              {0, 4},
                                              {1, 3},
                                              {1, 0},
                                              {1, 1},
                                              {1, 6},
                                              {1, 6},
                                              {1, 1}};

int analog_plan_range(unsigned channel,
                      unsigned range_index,
                      unsigned enabled_channels,
                      unsigned shared_register_shadow,
                      struct analog_range_plan *range_plan) {
    if (channel > 1 || range_index >= 14 || !range_plan) {
        return 0;
    }
    unsigned channel_bit_shift = channel * 4;
    shared_register_shadow = (shared_register_shadow & ~(15u << channel_bit_shift)) |
                             (ranges[range_index].bits << (channel_bit_shift + 1));
    shared_register_shadow &= 255;
    if (!(enabled_channels & 1)) {
        shared_register_shadow = (shared_register_shadow & ~15u) | (ranges[7].bits << 1);
    }
    if (!(enabled_channels & 2)) {
        shared_register_shadow = (shared_register_shadow & ~240u) | (ranges[7].bits << 5);
    }
    range_plan->shadow = shared_register_shadow;
    range_plan->writes[0] = (struct fpga_register_write){0x41c, shared_register_shadow};
    range_plan->writes[1] = (struct fpga_register_write){
        channel ? 0xc0 : 0xbc, (enabled_channels & (1u << channel)) ? ranges[range_index].bank : 1};
    range_plan->writes[2] = (struct fpga_register_write){0x4f0, 1};
    return 1;
}
