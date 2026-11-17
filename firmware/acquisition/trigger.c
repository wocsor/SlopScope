#include "scope_internal.h"

static int
trigger_levels(const struct scope_trigger *trigger_config, int *high, int *low, unsigned sampled);
static int trigger_program_one(const struct scope_trigger *trigger_config,
                               const struct scope_advanced_trigger *advanced_trigger,
                               unsigned family,
                               unsigned inverse);

static int
trigger_levels(const struct scope_trigger *trigger_config, int *high, int *low, unsigned sampled) {
    int high_threshold, low_threshold;
    unsigned channel_index = trigger_config->source;
    if (trigger_config->use_code || (!sampled && !scope_voltage_baseline())) {
        if (!scope_threshold(scope_state.channels.range[channel_index],
                             scope_state.channels.probe_multiplier[channel_index],
                             scope_state.channels.position_millidivisions[channel_index],
                             trigger_config,
                             &high_threshold,
                             &low_threshold)) {
            return -1;
        }
    } else {
        int code;
        if (!scope_voltage_code(channel_index,
                                scope_state.channels.range[channel_index],
                                scope_state.channels.probe_multiplier[channel_index],
                                scope_state.channels.offset_dac[channel_index],
                                trigger_config->level_uv,
                                &code)) {
            return -1;
        }
        high_threshold = code + (trigger_config->slope ? 4 : 0);
        low_threshold = code - (trigger_config->slope ? 0 : 4);
        if (high_threshold > 126) {
            high_threshold = 126;
        }
        if (low_threshold < -127) {
            low_threshold = -127;
        }
    }
    *high = high_threshold;
    *low = low_threshold;
    return 0;
}

static int trigger_program_one(const struct scope_trigger *trigger_config,
                               const struct scope_advanced_trigger *advanced_trigger,
                               unsigned family,
                               unsigned inverse) {

    struct scope_trigger physical = *trigger_config;
    physical.slope ^= inverse;
    if (inverse && !physical.use_code) {
        physical.level_uv = -physical.level_uv;
    }
    int high_threshold, low_threshold;
    unsigned channel_index = trigger_config->source;
    if (trigger_levels(&physical, &high_threshold, &low_threshold, 0)) {
        return -1;
    }
    unsigned addresses[] = {channel_index ? 0x314 : 0x30c,
                            channel_index ? 0x318 : 0x310,
                            channel_index ? 0x3f0 : 0x3ec,
                            channel_index ? 0x3f2 : 0x3ee};
    int values[] = {high_threshold, low_threshold, high_threshold, low_threshold};
    for (unsigned i = 0; i < 4; i++) {
        if (fpga_write_register(addresses[i], (uint32_t)values[i])) {
            return -1;
        }
    }
    if (advanced_trigger->mode) {
        int lower, upper;
        int32_t lower_uv = advanced_trigger->lower_uv, upper_uv = advanced_trigger->upper_uv;
        if (inverse) {
            int32_t swap = lower_uv;
            lower_uv = -upper_uv;
            upper_uv = -swap;
        }
        if (!scope_voltage_baseline()) {
            if (!scope_window_threshold(scope_state.channels.range[channel_index],
                                        scope_state.channels.probe_multiplier[channel_index],
                                        scope_state.channels.position_millidivisions[channel_index],
                                        lower_uv,
                                        &lower) ||
                !scope_window_threshold(scope_state.channels.range[channel_index],
                                        scope_state.channels.probe_multiplier[channel_index],
                                        scope_state.channels.position_millidivisions[channel_index],
                                        upper_uv,
                                        &upper)) {
                return -1;
            }
        } else if (!scope_voltage_code(channel_index,
                                       scope_state.channels.range[channel_index],
                                       scope_state.channels.probe_multiplier[channel_index],
                                       scope_state.channels.offset_dac[channel_index],
                                       lower_uv,
                                       &lower) ||
                   !scope_voltage_code(channel_index,
                                       scope_state.channels.range[channel_index],
                                       scope_state.channels.probe_multiplier[channel_index],
                                       scope_state.channels.offset_dac[channel_index],
                                       upper_uv,
                                       &upper)) {
            return -1;
        }
        struct scope_trigger_write plan[10];
        unsigned count;
        if (!scope_advanced_plan(advanced_trigger,
                                 channel_index,
                                 trigger_config->slope,
                                 trigger_config->sweep,
                                 inverse,
                                 trigger_config->holdoff_ns,
                                 lower,
                                 upper,
                                 plan,
                                 &count)) {
            return -1;
        }
        for (unsigned i = 0; i < count; i++) {
            if (fpga_write_register(plan[i].address,
                                    plan[i].value |
                                        (plan[i].address == 0x300 ? family << 17 : 0))) {
                return -1;
            }
            /* Stock ebfc0 sets Pulse coupling immediately after control, before timers */
            if (advanced_trigger->mode == 2 && plan[i].address == 0x300 &&
                fpga_write_register(channel_index ? 0xd0 : 0xcc,
                                    trigger_config->coupling ? 0 : 1)) {
                return -1;
            }
        }
        return fpga_write_register(0x8c, 2);
    }
    return fpga_write_register(0x300, scope_trigger_word(&physical) | (family << 17)) ||
           fpga_write_register(channel_index ? 0xd0 : 0xcc, trigger_config->coupling ? 0 : 1) ||
           fpga_write_register(channel_index ? 0x308 : 0x304,
                               scope_holdoff_word(trigger_config->holdoff_ns)) ||
           fpga_write_register(0x8c, 2);
}

void trigger_bank_store(void) {
    if (scope_state.configuration.features.trigger_family) {
        unsigned channel_index = scope_state.trigger.configured.source;
        scope_state.configuration.features.bank[channel_index].trigger =
            scope_state.trigger.configured;
        scope_state.configuration.features.bank[channel_index].advanced =
            scope_state.trigger.advanced;
    }
}

void video_control_normalize(struct scope_trigger *trigger_config,
                             struct scope_advanced_trigger *advanced_trigger) {
    if (advanced_trigger->mode == 1) {
        scope_video_normalize(advanced_trigger);
        trigger_config->sweep = 0;
    }
}

int trigger_program(void) {
    if (!scope_state.pulse.native && !scope_state.configuration.active_features.trigger_family &&
        scope_state.trigger.active_advanced.mode == 2) {
        struct scope_trigger candidate = scope_state.trigger.active_config;
        struct scope_advanced_trigger edge = scope_state.trigger.active_advanced;
        edge.mode = 0;
        scope_state.pulse.positive = (scope_state.trigger.active_advanced.condition < 3) ^
                                     scope_state.channels.inverse[candidate.source];
        candidate.slope =
            scope_state.pulse.positive ? 1 : 0; /* Candidate at the pulse's closing edge. */
        if (scope_state.channels.inverse[candidate.source] && !candidate.use_code) {
            candidate.level_uv = -candidate.level_uv;
        }
        if (candidate.sweep == 2) {
            candidate.sweep = 1;
        }
        if (trigger_levels(
                &candidate, &scope_state.pulse.high_code, &scope_state.pulse.low_code, 1)) {
            return -1;
        }
        return trigger_program_one(&candidate, &edge, 0, 0);
    }
    if (!scope_state.configuration.active_features.trigger_family) {
        return trigger_program_one(
            &scope_state.trigger.active_config,
            &scope_state.trigger.active_advanced,
            0,
            scope_state.channels.inverse[scope_state.trigger.active_config.source]);
    }
    for (unsigned channel_index = 0; channel_index < 2; channel_index++) {
        struct scope_trigger trigger_config =
            scope_state.configuration.active_features.bank[channel_index].trigger;
        trigger_config.source = channel_index;
        trigger_config.sweep = scope_state.trigger.active_config.sweep;
        if (trigger_program_one(
                &trigger_config,
                &scope_state.configuration.active_features.bank[channel_index].advanced,
                1,
                scope_state.channels.inverse[channel_index])) {
            return -1;
        }
        if (channel_index == 0) {
            display_delay_ms(1); /* ec3a8's bounded separation between ALT banks. */
        }
    }
    return 0;
}
