#ifndef SCOPE_ACQUISITION_H
#define SCOPE_ACQUISITION_H
#include <stdint.h>
struct scope_acquisition_control {
    uint32_t record_depth, horizontal_enabled;
    int32_t horizontal_ns;
};
struct scope_acquisition_geometry {
    uint32_t depth, pretrigger, posttrigger, address, compensation;
};
void scope_acquisition_defaults(struct scope_acquisition_control *acquisition_config);
int scope_acquisition_valid(const struct scope_acquisition_control *acquisition_config);
/* Supported CFW clock plans have stock rate*timebase ratio 10, interpolation 1 */
int scope_acquisition_geometry_ratio(unsigned sample_rate_hz,
                                     unsigned denominator,
                                     unsigned depth,
                                     int32_t time_ns,
                                     struct scope_acquisition_geometry *geometry);
int scope_acquisition_geometry(unsigned sample_rate_hz,
                               unsigned depth,
                               int32_t time_ns,
                               struct scope_acquisition_geometry *geometry);
#endif
