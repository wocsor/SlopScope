#include "diagnostics.h"
#include "scope_internal.h"

static unsigned int published_filling;
static uint32_t snapshot[1031];
static const uint32_t crc32_nibble_table[16] = {0,
                                                0x1db71064,
                                                0x3b6e20c8,
                                                0x26d930ac,
                                                0x76dc4190,
                                                0x6b6b51f4,
                                                0x4db26158,
                                                0x5005713c,
                                                0xedb88320,
                                                0xf00f9344,
                                                0xd6d6a3e8,
                                                0xcb61b38c,
                                                0x9b64c2b0,
                                                0x86d3d2d4,
                                                0xa00ae278,
                                                0xbdbdf21c};

static const uint8_t *scope_snapshot_aligned(unsigned *length, unsigned version);

unsigned capture_alignment(void) {
    return scope_state.capture.normal_mode &&
                   (scope_state.capture.stream_compression == 1 ||
                    scope_state.configuration.active_features.acquire_mode == 1)
               ? 1
               : 2;
}

int frame_channel_matches_live_range(unsigned channel_index) {
    if (!scope_state.capture.live_enabled) {
        return 1;
    }
    unsigned frame_range = scope_state.frame.words[8 + channel_index];
    return frame_range == scope_state.channels.range[channel_index] &&
           !(scope_state.analog.state == SETUP_RUNNING &&
             scope_state.analog.job_channel == channel_index &&
             scope_state.analog.job_range != frame_range);
}

void publish_frame(void) {
    scope_state.frame.channel_alignment = capture_alignment();
    scope_state.boot.console_active = 0;
    scope_state.frame.is_roll = scope_state.capture.capture_roll;
    published_filling = scope_state.capture.roll_filling;
    scope_state.frame.valid_start = 0;
    scope_state.frame.valid_count = 1998;
    if (scope_state.capture.capture_roll && scope_state.capture.roll_filling) {
        unsigned duration_ms =
            scope_state.capture.roll_transfer_started_ms - scope_state.capture.roll_fill_started_ms;
        scope_state.frame.valid_count =
            duration_ms > 100
                ? (unsigned)scope_ratio((int64_t)(duration_ms - 100) *
                                            sample_rate_numerator(scope_state.capture.rate_index),
                                        (uint64_t)1000 *
                                            sample_rate_denominator(scope_state.capture.rate_index))
                : 0;
        if (scope_state.frame.valid_count > 1998) {
            scope_state.frame.valid_count = 1998;
        }
        scope_state.frame.valid_start = 1998 - scope_state.frame.valid_count;
        published_filling = scope_state.frame.valid_count < 1998;
    }
    scope_state.frame.pulse_sampled = !scope_state.capture.capture_roll &&
                                      !scope_state.pulse.native && scope_state.trigger.active &&
                                      !scope_state.configuration.active_features.trigger_family &&
                                      scope_state.trigger.active_advanced.mode == 2;
    uint8_t *destination = (uint8_t *)(scope_state.frame.words + 16);
    uint32_t crc = 0xffffffffu;
    for (unsigned i = 0; i < 4000; i++) {
        destination[i] = CSI_SAMPLE_BUFFER[i];
    }
    if (scope_state.configuration.active_features.acquire_mode == 2 && !scope_state.record.active) {
        scope_average_process(&scope_state.configuration.averaged,
                              destination,
                              destination,
                              scope_state.configuration.active_features.average_count,
                              scope_state.capture.capture_average_epoch);
    }
    for (unsigned i = 0; i < 4000; i++) {
        unsigned bit = destination[i];
        crc ^= bit;
        crc = (crc >> 4) ^ crc32_nibble_table[crc & 15];
        crc = (crc >> 4) ^ crc32_nibble_table[crc & 15];
    }
    scope_state.frame.words[0] = 0x31435348;
    scope_state.frame.words[1] = 1;
    scope_state.frame.words[2] = 64;
    scope_state.frame.words[3] = 4000;
    for (unsigned channel_index = 0; channel_index < 2; channel_index++) {
        scope_state.frame.calibration_ids[channel_index] =
            scope_adc_calibration_id(channel_index, scope_state.channels.range[channel_index]);
    }
    scope_state.frame.words[4] = ++scope_state.frame.sequence;
    scope_state.frame.words[5] = sample_rates[scope_state.capture.rate_index][2];
    scope_state.frame.words[6] = ~timer_ticks();
    scope_state.frame.words[8] = scope_state.channels.range[0];
    scope_state.frame.words[9] = scope_state.channels.range[1];
    scope_state.frame.words[10] = scope_state.channels.coupling[0];
    scope_state.frame.words[11] = scope_state.channels.coupling[1];
    scope_state.frame.words[13] =
        scope_state.channels.offset_dac[0] | (scope_state.channels.offset_dac[1] << 16);
    scope_state.frame.words[14] = scope_state.capture.rate_index;
    scope_state.frame.words[15] = ~crc;
    scope_state.frame.extra_fields[0] = scope_state.configuration.active_features.acquire_mode;
    scope_state.frame.extra_fields[1] = scope_state.configuration.active_features.average_count;
    scope_state.frame.extra_fields[2] =
        scope_state.configuration.active_features.bandwidth[0] |
        (scope_state.configuration.active_features.bandwidth[1] << 1);
    scope_state.frame.extra_fields[3] = scope_state.configuration.active_features.trigger_family;
    scope_state.frame.extra_fields[4] =
        (uint32_t)(scope_state.configuration.active_acquisition.horizontal_enabled &&
                           !scope_state.capture.capture_roll
                       ? scope_state.configuration.active_acquisition.horizontal_ns
                       : 0);
    scope_state.frame.extra_fields[5] = scope_state.trigger.forced;
    scope_state.frame.extra_fields[6] = scope_state.trigger.active_advanced.mode;
    scope_state.frame.extra_fields[7] = scope_state.capture.epoch;
}

const uint8_t *scope_snapshot_legacy(unsigned *length) {
    *length = 0;
    if (!scope_state.frame.legacy_ready ||
        sample_rate_denominator(scope_state.frame.legacy_words[14]) != 1) {
        return 0;
    }
    for (unsigned i = 0; i < 1016; i++) {
        snapshot[i] = scope_state.frame.legacy_words[i];
    }
    *length = 4064;
    return (const uint8_t *)snapshot;
}

const uint8_t *scope_snapshot_v2(unsigned *length) {
    *length = 0;
    if (!scope_state.frame.legacy_ready ||
        sample_rate_denominator(scope_state.frame.legacy_words[14]) != 1) {
        return 0;
    }
    for (unsigned i = 0; i < 16; i++) {
        snapshot[i] = scope_state.frame.legacy_words[i];
    }
    snapshot[1] = 2;
    snapshot[2] = 96;
    for (unsigned i = 0; i < 8; i++) {
        snapshot[16 + i] = scope_state.frame.legacy_extra[i];
    }
    for (unsigned i = 0; i < 1000; i++) {
        snapshot[24 + i] = scope_state.frame.legacy_words[16 + i];
    }
    *length = 4096;
    return (const uint8_t *)snapshot;
}

static const uint8_t *scope_snapshot_aligned(unsigned *length, unsigned version) {
    *length = 0;
    if (version >= 5 ? !scope_state.frame.sequence : !scope_state.frame.legacy_ready) {
        return 0;
    }
    if (version < 6 &&
        sample_rate_denominator(version >= 5 ? scope_state.frame.words[14]
                                             : scope_state.frame.legacy_words[14]) != 1) {
        return 0;
    }
    const uint32_t *data = version >= 5 ? scope_state.frame.words : scope_state.frame.legacy_words,
                   *extra = version >= 5 ? scope_state.frame.extra_fields
                                         : scope_state.frame.legacy_extra;
    for (unsigned i = 0; i < 16; i++) {
        snapshot[i] = data[i];
    }
    snapshot[1] = version;
    snapshot[2] = version >= 7   ? 128
                  : version >= 6 ? 120
                  : version >= 5 ? 112
                  : version == 4 ? 100
                                 : 96;
    snapshot[3] = 3996;
    for (unsigned i = 0; i < 8; i++) {
        snapshot[16 + i] = extra[i];
    }
    unsigned alignment =
        version >= 5 ? scope_state.frame.channel_alignment : scope_state.frame.legacy_alignment;
    if (version >= 4) {
        snapshot[24] = (version >= 5 ? scope_state.frame.pulse_sampled
                                     : scope_state.frame.legacy_pulse_sampled) |
                       0x80u | (alignment << 4);
    }
    if (version >= 5) {
        snapshot[24] |= (scope_state.frame.is_roll << 1) | (published_filling << 2) |
                        (scope_state.frame.is_roll << 3);
        snapshot[25] = scope_state.frame.valid_start;
        snapshot[26] = scope_state.frame.valid_count;
        snapshot[27] = ~timer_ticks();
    }
    if (version >= 6) {
        snapshot[28] = sample_rate_numerator(data[14]);
        snapshot[29] = sample_rate_denominator(data[14]);
    }
    if (version >= 7) {
        snapshot[30] = scope_state.frame.calibration_ids[0];
        snapshot[31] = scope_state.frame.calibration_ids[1];
    }
    uint8_t *destination = (uint8_t *)(snapshot + (version >= 7   ? 32
                                                   : version >= 6 ? 30
                                                   : version >= 5 ? 28
                                                   : version == 4 ? 25
                                                                  : 24));
    const uint8_t *source = (const uint8_t *)(data + 16);
    uint32_t crc = 0xffffffffu;
    for (unsigned i = 0; i < 1998; i++) {
        destination[i] = source[i];
        destination[1998 + i] = source[2000 + alignment + i];
    }
    for (unsigned i = 0; i < 3996; i++) {
        crc ^= destination[i];
        crc = (crc >> 4) ^ crc32_nibble_table[crc & 15];
        crc = (crc >> 4) ^ crc32_nibble_table[crc & 15];
    }
    snapshot[15] = ~crc;
    *length = version >= 7   ? 4124
              : version >= 6 ? 4116
              : version >= 5 ? 4108
              : version == 4 ? 4096
                             : 4092;
    return (const uint8_t *)snapshot;
}

const uint8_t *scope_snapshot_v3(unsigned *length) {
    return scope_snapshot_aligned(length, 3);
}

const uint8_t *scope_snapshot_v4(unsigned *length) {
    return scope_snapshot_aligned(length, 4);
}

const uint8_t *scope_snapshot_v5(unsigned *length) {
    return scope_snapshot_aligned(length, 5);
}

const uint8_t *scope_snapshot_v6(unsigned *length) {
    return scope_snapshot_aligned(length, 6);
}

const uint8_t *scope_snapshot_v7(unsigned *length) {
    return scope_snapshot_aligned(length, 7);
}

#if USB_DIAGNOSTICS
const uint8_t *scope_diagnostic_saved_frame(unsigned *length) {
    *length = 0;
    if (snapshot[0] != 0x31435348u || snapshot[2] > 128 || snapshot[3] > 4000 ||
        snapshot[2] + snapshot[3] > sizeof snapshot) {
        return 0;
    }
    *length = snapshot[2] + snapshot[3];
    return (const uint8_t *)snapshot;
}
#endif
