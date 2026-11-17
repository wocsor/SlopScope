#include "scope_internal.h"

static unsigned record_sample_offset(void);

uint32_t *record_buffer(unsigned channel) {
    return (uint32_t *)(uintptr_t)(0x81010000u + channel * 0x4000u);
}

const uint8_t *scope_record_channel_snapshot(unsigned channel, unsigned *length) {
    *length = scope_state.record.ready && channel < 2 &&
                      (scope_state.record.channel_mask & (1u << channel))
                  ? 108 + scope_state.record.depth
                  : 0;
    return *length ? (const uint8_t *)record_buffer(channel) : 0;
}

const uint8_t *scope_record_snapshot(unsigned *length) {
    const uint32_t *source = (const uint32_t *)scope_record_channel_snapshot(
        scope_state.record.channel_mask & 1 ? 0 : 1, length);
    if (!*length) {
        return 0;
    }
    if (source[17] != 1) {
        *length = 0;
        return 0;
    } /* Legacy format cannot describe fractional Hz. */
    uint32_t *legacy = (uint32_t *)0x81018000u;
    for (unsigned i = 0; i < 16; i++) {
        legacy[i] = source[i];
    }
    legacy[1] = 1;
    legacy[2] = 64;
    uint8_t *destination_samples = (uint8_t *)(legacy + 16);
    const uint8_t *source_samples = (const uint8_t *)(source + 27);
    for (unsigned i = 0; i < scope_state.record.depth; i++) {
        destination_samples[i] = source_samples[i];
    }
    *length = 64 + scope_state.record.depth;
    return (const uint8_t *)legacy;
}

static unsigned record_sample_offset(void) {
    unsigned full = 10000 + (48 - (5000 % 48)) * 2;
    unsigned selected = scope_state.record.depth + (48 - ((scope_state.record.depth / 2) % 48)) * 2;
    return full - selected;
}

void publish_record(void) {
    uint32_t *buffer = record_buffer(scope_state.record.channel);
    uint8_t *destination = (uint8_t *)(buffer + 27);
    for (unsigned i = 0; i < scope_state.record.depth; i++) {
        destination[i] = CSI_SAMPLE_BUFFER[record_sample_offset() + i];
    }
    buffer[0] = 0x31525348;
    buffer[1] = 4;
    buffer[2] = 108;
    buffer[3] = scope_state.record.depth;
    buffer[4] = scope_state.record.sequence;
    buffer[5] = sample_rates[scope_state.capture.rate_index][2];
    buffer[6] = scope_state.record.completed_ticks;
    buffer[7] = scope_state.capture.csi_flags;
    buffer[8] = scope_state.record.channel;
    buffer[9] = scope_state.channels.range[scope_state.record.channel];
    buffer[10] = scope_state.channels.coupling[scope_state.record.channel];
    buffer[11] = scope_state.channels.offset_dac[scope_state.record.channel];
    buffer[12] = scope_state.record.depth;
    buffer[13] = scope_state.trigger.active_advanced.mode;
    buffer[14] = (uint32_t)(scope_state.configuration.active_acquisition.horizontal_enabled
                                ? scope_state.configuration.active_acquisition.horizontal_ns
                                : 0);
    buffer[15] = scope_crc32(destination, scope_state.record.depth);
    buffer[16] = sample_rate_numerator(scope_state.capture.rate_index);
    buffer[17] = sample_rate_denominator(scope_state.capture.rate_index);
    buffer[18] = scope_state.record.forced;
    buffer[19] = scope_state.capture.epoch;
    struct scope_acquisition_geometry geometry;
    scope_acquisition_geometry_ratio(
        sample_rate_numerator(scope_state.capture.rate_index),
        sample_rate_denominator(scope_state.capture.rate_index),
        scope_state.record.depth,
        scope_state.configuration.active_acquisition.horizontal_enabled
            ? scope_state.configuration.active_acquisition.horizontal_ns
            : 0,
        &geometry);
    buffer[20] = geometry.pretrigger;
    buffer[21] = geometry.posttrigger;
    buffer[22] = scope_state.record.armed_ticks;
    buffer[23] = scope_state.record.completed_ticks;
    buffer[24] = !scope_state.pulse.native && scope_state.trigger.active_advanced.mode == 2 &&
                 !scope_state.configuration.active_features.trigger_family;
    buffer[25] = scope_state.record.channel;
    buffer[26] = scope_adc_calibration_id(scope_state.record.channel,
                                          scope_state.channels.range[scope_state.record.channel]);
}

int record_read_begin(void) {
    unsigned height = (record_sample_offset() + scope_state.record.depth + 999) / 1000;
    for (unsigned i = 0; i < 5; i++) {
        if (fpga_write_register(0x27c, 1)) {
            return -1;
        }
    }
    for (unsigned i = 0; i < 5; i++) {
        if (fpga_write_register(0x4fc, scope_state.record.channel)) {
            return -1;
        }
    }
    for (unsigned i = 0; i < 5; i++) {
        if (fpga_write_register(CAP_COUNT, 1000)) {
            return -1;
        }
    }
    for (unsigned i = 0; i < 5; i++) {
        if (fpga_write_register(CAP_HEIGHT, height)) {
            return -1;
        }
    }
    for (unsigned i = 0; i < height * 1000; i++) {
        CSI_SAMPLE_BUFFER[i] = 0x55;
    }
    MMIO32(CSI_BASE + 0x40) = 80u | (1000u << 16);
    MMIO32(CSI_BASE + 0x44) = 40u | (height << 16);
    MMIO32(CSI_BASE + 0x48) = 1000;
    MMIO32(CSI_BASE + 0x10) = (uintptr_t)CSI_SAMPLE_BUFFER;
    MMIO32(CSI_BASE + 0x34) = 0xffffffffu;
    MMIO32(CSI_BASE + 8) |= 1u;
    for (unsigned i = 0; i < 5; i++) {
        if (fpga_write_register(CAP_ARM, 1)) {
            return -1;
        }
    }
    scope_state.record.reading = 1;
    scope_state.capture.state = CAPTURE_WAITING;
    scope_state.capture.started_ticks = timer_ticks();
    scope_state.capture.started_ms = timer_milliseconds();
    return 0;
}
