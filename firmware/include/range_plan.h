#ifndef SCOPE_ANALOG_RANGE_PLAN_H
#define SCOPE_ANALOG_RANGE_PLAN_H
struct fpga_register_write {
    unsigned address, value;
};
struct analog_range_plan {
    unsigned shadow;
    struct fpga_register_write writes[3];
};
int analog_plan_range(unsigned channel,
                      unsigned range_index,
                      unsigned enabled_channels,
                      unsigned shared_register_shadow,
                      struct analog_range_plan *range_plan);
#endif
