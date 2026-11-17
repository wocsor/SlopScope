#include "scope_internal.h"

static unsigned int roll_epoch;
static unsigned int roll_rate;
static unsigned int roll_transfer_pending;
static uint32_t roll_transfer_started;
static uint32_t pulse_auto_started;
static unsigned int pulse_epoch;
static unsigned int trigger_user_forced;
static unsigned int capture_auto_ms;
static uint32_t record_fill_started;
static uint32_t live_started;
static unsigned int capture_index;
static uint32_t capture_begin;

static void capture_fail(unsigned reason);
static unsigned capture_wait_limit_ms(void);
static unsigned capture_auto_wait_ms(void);

void capture_stop(void) {
    MMIO32(CSI_BASE + 8) &= ~3u;
    MMIO32(CSI_BASE + 0x34) = 0xffffffffu;
}

void capture_abort_wait(void) {
    if (scope_state.capture.state >= CAPTURE_CONFIGURING &&
        scope_state.capture.state <= CAPTURE_TRANSFERRING) {
        unsigned transferring = scope_state.capture.state == CAPTURE_TRANSFERRING;
        capture_stop();
        scope_state.capture.state = CAPTURE_IDLE;
        if (transferring) {
            for (unsigned i = 0; i < 6; i++) {
                if (fpga_write_register(CAP_ACK, 1)) {
                    scope_state.capture.error = 1;
                    scope_state.capture.state = CAPTURE_FAILED;
                    break;
                }
            }
        }
        if (scope_state.record.active) {
            scope_state.record.active = 0;
            scope_state.record.ready = scope_state.record.filling = scope_state.record.reading = 0;
        }
    }
}

static void capture_fail(unsigned reason) {
    capture_stop();
    scope_state.capture.error = reason;
    scope_state.capture.state = CAPTURE_FAILED;
    scope_state.capture.live_enabled = 0;
    scope_state.record.active = 0;
}

void capture_start(void) {
    video_control_normalize(&scope_state.trigger.configured, &scope_state.trigger.advanced);
    trigger_bank_store();
    scope_state.configuration.active_features = scope_state.configuration.features;
    scope_state.configuration.active_acquisition = scope_state.configuration.acquisition;
    scope_state.capture.capture_average_epoch = scope_state.configuration.average_epoch;
    scope_state.record.filling = scope_state.record.reading = scope_state.record.forced = 0;
    if (scope_state.record.active) {
        scope_state.record.channel = scope_state.record.first_channel;
    }
    capture_begin = timer_ticks();
    scope_state.capture.setup_started_ms = timer_milliseconds();
    scope_state.capture.epoch = scope_state.capture.live_epoch;
    scope_state.trigger.active_config = scope_state.trigger.configured;
    scope_state.trigger.active_advanced = scope_state.trigger.advanced;
    scope_state.trigger.active = scope_state.trigger.enabled;
    scope_state.trigger.forced = trigger_user_forced = 0;
    if (scope_state.capture.pending_rate_index) {
        scope_state.capture.rate_index = scope_state.capture.pending_rate_index;
        scope_state.capture.pending_rate_index = 0;
    }
    if (!scope_state.pulse.candidates || pulse_epoch != scope_state.capture.capture_average_epoch) {
        pulse_epoch = scope_state.capture.capture_average_epoch;
        pulse_auto_started = timer_ticks();
    }
    scope_state.capture.capture_roll =
        scope_state.capture.normal_mode && !scope_state.record.active &&
        scope_state.capture.rate_index >= 16 && scope_state.trigger.active_config.sweep != 2 &&
        scope_state.capture.roll_live &&
        !scope_state.configuration.active_features.trigger_family &&
        !scope_state.configuration.active_acquisition.horizontal_enabled;
    if (!scope_state.capture.capture_roll) {
        scope_state.capture.roll_configured = 0;
        scope_state.capture.roll_filling = 0;
    } else if (scope_state.configuration.active_features.acquire_mode == 2) {
        scope_state.configuration.active_features.acquire_mode = 0;
        scope_state.configuration.features.acquire_mode = 0;
        scope_state.configuration.averaged.initialized = 0;
    }
    MMIO32(0x01c20064u) |= 1u << 8;
    MMIO32(0x01c202c4u) &= ~(1u << 8);
    display_delay_ms(1);
    MMIO32(0x01c202c4u) |= 1u << 8;
    MMIO32(0x01c20100u) |= 2;
    MMIO32(0x01c20134u) = 0x8500;
    for (unsigned i = 0; i < 11; i++) {
        gpio_set_function(GPIOE_BASE, i, 2);
    }
    gpio_set_function(GPIOE_BASE, 11, 0);
    MMIO32(CSI_BASE) = 1;
    MMIO32(CSI_BASE + 4) = 0x400;
    MMIO32(CSI_BASE + 0x30) = 0;
    MMIO32(CSI_BASE + 8) = 0;
    MMIO32(CSI_BASE + 0x34) = 0xffffffffu;
    capture_index = scope_state.capture.error = scope_state.capture.csi_flags =
        scope_state.capture.changed_samples = roll_transfer_pending = 0;
    scope_state.capture.state = CAPTURE_CONFIGURING;
    if (scope_state.trigger.active && scope_state.capture.normal_mode &&
        !scope_state.capture.capture_roll && !scope_state.record.active && trigger_program()) {
        capture_fail(1);
    }
}

int capture_busy(void) {
    return scope_state.capture.state >= CAPTURE_CONFIGURING &&
           scope_state.capture.state <= CAPTURE_TRANSFERRING;
}

void live_poll(void) {
    if (scope_state.capture.live_enabled && !capture_busy() &&
        scope_state.analog.state != SETUP_RUNNING && scope_state.adc.state != SETUP_RUNNING &&
        timer_has_elapsed(live_started, 0)) {
        capture_start();
    }
}

static unsigned capture_wait_limit_ms(void) {
    if (scope_state.record.reading) {
        return 15000;
    }
    if (scope_state.capture.capture_roll) {
        return scope_state.capture.rate_index >= 28 ? 45000 : 15000;
    }
    unsigned depth = scope_state.record.active ? scope_state.record.depth : 10000;
    unsigned history = (unsigned)scope_ratio(
        (int64_t)depth * 1000 * sample_rate_denominator(scope_state.capture.rate_index),
        sample_rate_numerator(scope_state.capture.rate_index));
    return history > 10000 ? history + 5000 : 15000;
}

static unsigned capture_auto_wait_ms(void) {
    /* Video may need a field pair to lock before the requested sample history * completes */
    if (scope_state.trigger.active_advanced.mode != 1) {
        return 50;
    }
    struct scope_acquisition_geometry geometry;
    if (!scope_acquisition_geometry_ratio(
            sample_rate_numerator(scope_state.capture.rate_index),
            sample_rate_denominator(scope_state.capture.rate_index),
            scope_state.record.active ? scope_state.record.depth : 10000,
            scope_state.configuration.active_acquisition.horizontal_enabled
                ? scope_state.configuration.active_acquisition.horizontal_ns
                : 0,
            &geometry)) {
        return 50;
    }
    unsigned history =
        (unsigned)scope_ratio((int64_t)(geometry.pretrigger + geometry.posttrigger) * 1000 *
                                      sample_rate_denominator(scope_state.capture.rate_index) +
                                  sample_rate_numerator(scope_state.capture.rate_index) - 1,
                              sample_rate_numerator(scope_state.capture.rate_index));
    return 50 + history;
}

static void capture_poll_configuration(void) {
    static const unsigned normal_prefix[][2] = {
        {0xac, 1}, {0xb0, 1}, {0x180, 0}, {0x27c, 0}, {0x154, 1}};
    unsigned continuing_roll = scope_state.capture.capture_roll &&
                               scope_state.capture.roll_configured &&
                               roll_epoch == scope_state.capture.capture_average_epoch &&
                               roll_rate == scope_state.capture.rate_index;
    unsigned setup_total = continuing_roll ? 0
                           : scope_state.capture.normal_mode
                               ? sizeof(normal_prefix) / sizeof(normal_prefix[0])
                               : sizeof(selftest_setup) / sizeof(selftest_setup[0]);
    /* Bounded batches reduce polling overhead without starving USB. */
    for (unsigned batch = 0; batch < 4 && capture_index < setup_total; batch++) {
        const unsigned *setup = scope_state.capture.normal_mode ? normal_prefix[capture_index]
                                                                : selftest_setup[capture_index];
        if (fpga_write_register(setup[0], setup[1])) {
            capture_fail(1);
            return;
        }
        if (!scope_state.capture.normal_mode && capture_index == 22) {
            display_delay_ms(1);
        }
        capture_index++;
    }
    if (capture_index < setup_total) {
        return;
    }
    if (!continuing_roll &&
        fpga_write_register(0xa4,
                            !scope_state.record.active &&
                                    scope_state.configuration.active_features.acquire_mode == 1
                                ? 1
                                : 0)) {
        capture_fail(1);
        return;
    }
    /* Replay the known fresh acquisition setup */
    for (unsigned i = 0;
         !continuing_roll &&
         i < rate_plan_counts[scope_state.capture.rate_index > 18 ? 18
                                                                  : scope_state.capture.rate_index];
         i++) {
        const unsigned (*plan)[2] =
            scope_state.capture.capture_roll
                ? roll_plans[scope_state.capture.rate_index > 18
                                 ? 2
                                 : scope_state.capture.rate_index - 16]
                : rate_plans[scope_state.capture.rate_index > 18 ? 18
                                                                 : scope_state.capture.rate_index];
        unsigned address = plan[i][0], value = plan[i][1];
        if (scope_state.capture.rate_index > 18) {
            if (address == 0x144) {
                value = sample_rates[scope_state.capture.rate_index][1];
            }
            if (address == 0x110) {
                value = scope_state.capture.rate_index + 7;
            }
        }
        /* Stock's compressed display window starts at 2400 in a 10K ring */
        if (scope_state.capture.capture_roll && address == 0x200) {
            value = 8000;
        }
        if (scope_state.record.active && address == 0xa8) {
            value = 1; /* Warm the new clock in continuous mode. */
        }
        /* Normal raw samples use compression 1 */
        if (scope_state.capture.normal_mode && !scope_state.record.active &&
            scope_state.capture.stream_compression && address == 0x204) {
            value = scope_state.configuration.active_features.acquire_mode == 1 &&
                            scope_state.capture.stream_compression == 1
                        ? 2
                        : scope_state.capture.stream_compression;
        }
        if (fpga_write_register(address, value)) {
            capture_fail(1);
            return;
        }
    }
    if (scope_state.record.active ||
        scope_state.configuration.active_acquisition.horizontal_enabled) {
        struct scope_acquisition_geometry geometry;
        if (!scope_acquisition_geometry_ratio(
                sample_rate_numerator(scope_state.capture.rate_index),
                sample_rate_denominator(scope_state.capture.rate_index),
                scope_state.record.active ? scope_state.record.depth : 10000,
                scope_state.configuration.active_acquisition.horizontal_enabled
                    ? scope_state.configuration.active_acquisition.horizontal_ns
                    : 0,
                &geometry)) {
            capture_fail(5);
            return;
        }
        if (fpga_write_register(0x200, geometry.address) ||
            fpga_write_register(0x14c, geometry.pretrigger) ||
            fpga_write_register(0x148, geometry.posttrigger) ||
            fpga_write_register(0x150, geometry.depth) ||
            fpga_write_register(0x11c, geometry.compensation)) {
            capture_fail(1);
            return;
        }
    }
    if (fpga_write_register(0x4fc, 0)) {
        capture_fail(1);
        return;
    }
    if (scope_state.record.active) {
        struct scope_acquisition_geometry roll_geometry;
        if (!scope_acquisition_geometry_ratio(
                sample_rate_numerator(scope_state.capture.rate_index),
                sample_rate_denominator(scope_state.capture.rate_index),
                scope_state.record.depth,
                scope_state.configuration.active_acquisition.horizontal_enabled
                    ? scope_state.configuration.active_acquisition.horizontal_ns
                    : 0,
                &roll_geometry)) {
            capture_fail(5);
            return;
        }
        scope_state.record.fill_ms =
            (unsigned)scope_ratio((int64_t)(roll_geometry.pretrigger + roll_geometry.posttrigger) *
                                      1000 *
                                      sample_rate_denominator(scope_state.capture.rate_index),
                                  sample_rate_numerator(scope_state.capture.rate_index)) +
            100;
        record_fill_started = timer_ticks();
        scope_state.record.fill_started_ms = timer_milliseconds();
        scope_state.record.filling = 1;
        if (fpga_write_register(0x300, 0)) {
            capture_fail(1);
            return;
        }
    }
    /* Stock AUTO trigger polling writes table+0x11c (FPGA 0x58) = 3 * when neither trigger has fired */
    if (scope_state.capture.normal_mode && !scope_state.capture.capture_roll &&
        !scope_state.record.active && !scope_state.trigger.active && fpga_write_register(0x58, 3)) {
        capture_fail(1);
        return;
    }
    unsigned height = 4; /* Complete a normal acquisition before entering deep readout. */
    if (fpga_write_register(CAP_COUNT, 1000) || fpga_write_register(CAP_HEIGHT, height)) {
        capture_fail(1);
        return;
    }
    for (unsigned i = 0; i < 4000; i++) {
        CSI_SAMPLE_BUFFER[i] = 0x55;
    }
    MMIO32(CSI_BASE + 0x40) = 80u | (1000u << 16);
    MMIO32(CSI_BASE + 0x44) = 40u | (height << 16);
    MMIO32(CSI_BASE + 0x48) = 1000;
    MMIO32(CSI_BASE + 0x10) = (uintptr_t)CSI_SAMPLE_BUFFER;
    MMIO32(CSI_BASE + 0x34) = 0xffffffffu;
    MMIO32(CSI_BASE + 8) |= 1u;
    if ((!scope_state.capture.normal_mode || scope_state.capture.capture_roll) &&
        fpga_write_register(CAP_ARM, 1)) {
        capture_fail(1);
        return;
    }
    if (scope_state.capture.capture_roll) {
        if (!continuing_roll) {
            scope_state.capture.roll_filling = 1;
            scope_state.capture.roll_fill_started = timer_ticks();
            scope_state.capture.roll_fill_started_ms = timer_milliseconds();
            scope_state.capture.roll_fill_ms =
                (unsigned)scope_ratio((int64_t)1998000 *
                                          sample_rate_denominator(scope_state.capture.rate_index),
                                      sample_rate_numerator(scope_state.capture.rate_index)) +
                100;
            scope_state.display.display_redraw_requested = 1;
        }
        scope_state.capture.roll_configured = 1;
        roll_epoch = scope_state.capture.capture_average_epoch;
        roll_rate = scope_state.capture.rate_index;
    }
    capture_auto_ms = capture_auto_wait_ms();
    scope_state.capture.started_ticks = timer_ticks();
    scope_state.capture.started_ms = timer_milliseconds();
    scope_state.capture.capture_setup_ticks = capture_begin - scope_state.capture.started_ticks;
    scope_state.capture.state = CAPTURE_WAITING;
}

static void capture_poll_trigger(void) {
    if (scope_state.record.active && scope_state.record.filling) {
        if (timer_elapsed_ms(scope_state.record.fill_started_ms) < scope_state.record.fill_ms) {
            return;
        }
        /* 0x54 belongs to readout. Stock 9def0 first completes normal acquisition. */
        if (fpga_write_register(0xa8, 0) || (scope_state.trigger.active && trigger_program()) ||
            fpga_write_register(0x154, 1)) {
            capture_fail(1);
            return;
        }
        for (unsigned i = 0; i < 5; i++) {
            if (fpga_write_register(0x50, 1)) {
                capture_fail(1);
                return;
            }
        }
        scope_state.record.filling = 0;
        scope_state.record.armed_ticks = ~timer_ticks();
        scope_state.capture.started_ticks = timer_ticks();
        scope_state.capture.started_ms = timer_milliseconds();
        if (!scope_state.trigger.active) {
            if (fpga_write_register(0x58, 3)) {
                capture_fail(1);
                return;
            }
            scope_state.trigger.forced = 1;
        }
        return;
    }
    if (!scope_state.capture.capture_roll && !scope_state.record.reading &&
        scope_state.trigger.active && !gpio_read(GPIOE_BASE, 11) && !scope_state.trigger.forced &&
        (scope_state.trigger.force_requested ||
         (scope_state.trigger.active_config.sweep == 0 &&
          (timer_elapsed_ms(scope_state.capture.started_ms) >= capture_auto_ms)))) {
        if (fpga_write_register(0x58, 3)) {
            capture_fail(1);
            return;
        }
        trigger_user_forced = scope_state.trigger.force_requested != 0;
        scope_state.trigger.forced = 1;
        scope_state.trigger.force_requested = 0;
    }
    if (scope_state.capture.capture_roll && gpio_read(GPIOE_BASE, 11) && !roll_transfer_pending) {
        if (fpga_write_register(CAP_ARM, 1)) {
            capture_fail(1);
            return;
        }
        roll_transfer_pending = 1;
        roll_transfer_started = timer_ticks();
        scope_state.capture.roll_transfer_started_ms = timer_milliseconds();
        return;
    }
    if (scope_state.capture.capture_roll && roll_transfer_pending &&
        !timer_has_elapsed(roll_transfer_started, 30)) {
        return;
    }
    if ((!scope_state.capture.capture_roll || roll_transfer_pending) && gpio_read(GPIOE_BASE, 11)) {
        for (unsigned i = 0; i < 6; i++) {
            if (fpga_write_register(CAP_TRANSFER, 1)) {
                capture_fail(1);
                return;
            }
        }
        for (unsigned i = 0; (!scope_state.record.active || !scope_state.record.reading) && i < 5;
             i++) {
            if (fpga_write_register(CAP_CONTROL, 0)) {
                capture_fail(1);
                return;
            }
        }
        scope_state.capture.state = CAPTURE_TRANSFERRING;
        scope_state.capture.started_ticks = timer_ticks();
        scope_state.capture.started_ms = timer_milliseconds();
    } else if ((scope_state.capture.capture_roll || scope_state.record.reading ||
                !scope_state.trigger.active || scope_state.trigger.forced) &&
               (timer_elapsed_ms(scope_state.capture.started_ms) >= capture_wait_limit_ms())) {
        capture_fail(2);
    }
}

static void capture_poll_readout(void) {
    scope_state.capture.csi_flags = MMIO32(CSI_BASE + 0x34);
    if (scope_state.capture.csi_flags & 0x7c) {
        capture_fail(3);
        return;
    }
    if (scope_state.capture.csi_flags & 1) {
        capture_stop();
        for (unsigned i = 0; i < 6; i++) {
            if (fpga_write_register(CAP_ACK, 1)) {
                capture_fail(1);
                return;
            }
        }
        if (scope_state.record.active) {
            if (!scope_state.record.reading) {
                scope_state.record.completed_ticks = ~timer_ticks();
                if (scope_state.trigger.force_requested) {
                    trigger_user_forced = 1;
                }
                scope_state.record.forced =
                    scope_state.trigger.forced || scope_state.trigger.force_requested;
                scope_state.trigger.force_requested = 0;
                if (record_read_begin()) {
                    capture_fail(1);
                }
                return;
            }
            publish_record();
            if (scope_state.record.channel_mask == 3 && scope_state.record.channel == 0) {
                scope_state.record.channel = 1;
                if (record_read_begin()) {
                    capture_fail(1);
                }
                return;
            }
            if (!scope_state.pulse.native && scope_state.trigger.active &&
                !scope_state.configuration.active_features.trigger_family &&
                scope_state.trigger.active_advanced.mode == 2) {
                const uint8_t *samples =
                    (const uint8_t *)(record_buffer(scope_state.trigger.active_config.source) + 27);
                scope_state.pulse.candidates++;
                int match =
                    scope_pulse_match_ratio(samples,
                                            scope_state.record.depth,
                                            sample_rate_numerator(scope_state.capture.rate_index),
                                            sample_rate_denominator(scope_state.capture.rate_index),
                                            scope_state.pulse.high_code,
                                            scope_state.pulse.low_code,
                                            scope_state.pulse.positive,
                                            scope_state.trigger.active_advanced.condition % 3,
                                            scope_state.trigger.active_advanced.width_ns);
                unsigned user_forced = trigger_user_forced || scope_state.trigger.force_requested;
                if (match) {
                    scope_state.pulse.accepted++;
                    scope_state.record.forced = user_forced;
                } else {
                    scope_state.pulse.rejected++;
                    if (!user_forced && (scope_state.trigger.active_config.sweep != 0 ||
                                         !timer_has_elapsed(pulse_auto_started, 50))) {
                        scope_state.trigger.force_requested = 0;
                        capture_start();
                        return;
                    }
                    scope_state.record.forced = 1;
                    if (!user_forced) {
                        scope_state.pulse.auto_fallbacks++;
                    }
                }
                pulse_auto_started = timer_ticks();
                for (unsigned channel_index = 0; channel_index < 2; channel_index++) {
                    if (scope_state.record.channel_mask & (1u << channel_index)) {
                        record_buffer(channel_index)[18] = scope_state.record.forced;
                    }
                }
            }
            scope_state.record.ready = 1;
            scope_state.record.active = scope_state.record.filling = scope_state.record.reading = 0;
            scope_state.capture.state = CAPTURE_COMPLETE;
            scope_state.trigger.force_requested = 0;
            live_started = timer_ticks();
            return;
        }
        for (unsigned i = 0; i < 4000; i++) {
            if (CSI_SAMPLE_BUFFER[i] != 0x55) {
                scope_state.capture.changed_samples++;
            }
        }
        scope_state.capture.state = CAPTURE_COMPLETE;
        if (scope_state.trigger.force_requested) {
            trigger_user_forced = scope_state.trigger.forced = 1;
        }
        scope_state.trigger.force_requested = 0;
        if (scope_state.capture.capture_roll && scope_state.capture.roll_filling &&
            scope_state.capture.roll_transfer_started_ms -
                    scope_state.capture.roll_fill_started_ms >=
                scope_state.capture.roll_fill_ms) {
            scope_state.capture.roll_filling = 0;
            scope_state.display.display_redraw_requested = 1;
        }
        if (!scope_state.capture.capture_roll && !scope_state.pulse.native &&
            scope_state.trigger.active &&
            !scope_state.configuration.active_features.trigger_family &&
            scope_state.trigger.active_advanced.mode == 2) {
            scope_state.pulse.candidates++;
            unsigned channel_index = scope_state.trigger.active_config.source;
            int match = scope_pulse_match_ratio(
                CSI_SAMPLE_BUFFER + (channel_index ? 2000 + capture_alignment() : 0),
                1998,
                sample_rate_numerator(scope_state.capture.rate_index),
                sample_rate_denominator(scope_state.capture.rate_index),
                scope_state.pulse.high_code,
                scope_state.pulse.low_code,
                scope_state.pulse.positive,
                scope_state.trigger.active_advanced.condition % 3,
                scope_state.trigger.active_advanced.width_ns);
            if (match) {
                scope_state.pulse.accepted++;
                scope_state.trigger.forced = 0;
            } else {
                scope_state.pulse.rejected++;
                if (!trigger_user_forced) {
                    if (scope_state.trigger.active_config.sweep != 0 ||
                        !timer_has_elapsed(pulse_auto_started, 50)) {
                        live_started = timer_ticks();
                        return;
                    }
                    scope_state.trigger.forced = 1;
                    scope_state.pulse.auto_fallbacks++;
                }
            }
            pulse_auto_started = timer_ticks();
        }
        if (scope_state.trigger.active && scope_state.trigger.active_config.sweep == 2 &&
            scope_state.capture.epoch == scope_state.capture.live_epoch) {
            scope_state.capture.live_enabled = 0;
        }
        if (scope_state.capture.capture_roll) {
            scope_state.trigger.forced = 1; /* Free-running preview is never a qualified trigger. */
        }
        uint32_t process_start = timer_ticks();
        scope_state.capture.capture_wait_ticks = scope_state.capture.started_ticks - process_start;
        publish_frame();
        scope_state.frame.words[7] = scope_state.capture.csi_flags;
        scope_state.frame.words[12] = scope_state.adc.mode;
        if (scope_state.frame.valid_count == 1998) {
            for (unsigned i = 0; i < 1016; i++) {
                scope_state.frame.legacy_words[i] = scope_state.frame.words[i];
            }
            for (unsigned i = 0; i < 8; i++) {
                scope_state.frame.legacy_extra[i] = scope_state.frame.extra_fields[i];
            }
            scope_state.frame.legacy_pulse_sampled = scope_state.frame.pulse_sampled;
            scope_state.frame.legacy_alignment = scope_state.frame.channel_alignment;
            scope_state.frame.legacy_ready = 1;
        }
        live_started = timer_ticks();
        scope_state.capture.capture_process_ticks = process_start - live_started;
    } else if (timer_has_elapsed(scope_state.capture.started_ticks,
                                 scope_state.capture.rate_index >= 28   ? 45000
                                 : scope_state.capture.rate_index >= 16 ? 15000
                                                                        : 2000)) {
        capture_fail(4);
    }
}

void capture_poll(void) {
    if (scope_state.capture.state == CAPTURE_CONFIGURING) {
        capture_poll_configuration();
    }
    if (scope_state.capture.state == CAPTURE_WAITING &&
        timer_elapsed_ms(scope_state.capture.started_ms) >= 3) {
        capture_poll_trigger();
    }
    if (scope_state.capture.state == CAPTURE_TRANSFERRING) {
        capture_poll_readout();
    }
}

unsigned capture_begin_live(unsigned intent, uint8_t *reply) {
    if (intent == 2 && scope_state.trigger.advanced.mode == 1) {
        return reply_write_text(reply, "ERR video triggering uses AUTO sweep\n");
    }
    video_control_normalize(&scope_state.trigger.configured, &scope_state.trigger.advanced);
    if (intent == 2 && scope_state.configuration.features.trigger_family) {
        return reply_write_text(reply, "ERR alternate triggering uses AUTO sweep\n");
    }
    if (!scope_state.analog.configured || scope_state.adc.state != SETUP_COMPLETE ||
        scope_state.adc.mode != 1 || scope_state.fpga.state != FPGA_CONFIG_READY) {
        return reply_write_text(reply, "ERR initialize normal ADC and analog first\n");
    }
    if (intent == 1 && scope_state.trigger.configured.sweep == 2) {
        scope_state.trigger.configured.sweep = scope_state.capture.continuous_sweep;
        capture_abort_wait();
    }
    if (intent == 2) {
        if (scope_state.trigger.configured.sweep != 2) {
            scope_state.capture.continuous_sweep = scope_state.trigger.configured.sweep;
        }
        scope_state.trigger.configured.sweep = 2;
        scope_state.trigger.enabled = 1;
        capture_abort_wait();
    }
    scope_state.trigger.force_requested = 0;
    scope_state.capture.live_epoch++;
    scope_state.capture.live_enabled = 1;
    live_started = timer_ticks() - 20u * 24000u;
    if (scope_state.display.screen_on && scope_state.display.draw_buffer_valid) {
        scope_state.display.display_redraw_requested = 1;
    }
    return reply_write_text(reply, intent == 2 ? "OK single armed\n" : "OK live started\n");
}
