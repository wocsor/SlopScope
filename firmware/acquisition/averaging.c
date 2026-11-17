#include "features.h"
void scope_features_defaults(struct scope_features *features_config) {
    *features_config = (struct scope_features)SCOPE_FEATURES_DEFAULT;
}
int scope_features_valid(const struct scope_features *features_config) {
    if (!features_config || features_config->bandwidth[0] > 1 ||
        features_config->bandwidth[1] > 1 || features_config->acquire_mode > 2 ||
        features_config->trigger_family > 1 ||
        (features_config->average_count != 4 && features_config->average_count != 16 &&
         features_config->average_count != 64 && features_config->average_count != 128)) {
        return 0;
    }
    for (unsigned channel_index = 0; channel_index < 3; channel_index++) {
        const struct scope_trigger_bank *bank =
            channel_index < 2 ? &features_config->bank[channel_index] : &features_config->normal;
        const struct scope_trigger *trigger_config = &bank->trigger;
        if ((channel_index < 2 ? trigger_config->source != channel_index
                               : trigger_config->source > 1) ||
            trigger_config->slope > 1 || trigger_config->sweep > 2 ||
            trigger_config->coupling > 1 || trigger_config->use_code > 1 ||
            trigger_config->code < -127 || trigger_config->code > 126 ||
            trigger_config->level_uv < -1000000000 || trigger_config->level_uv > 1000000000 ||
            trigger_config->holdoff_ns < 100 || trigger_config->holdoff_ns > 1000000000 ||
            !scope_advanced_valid(&bank->advanced)) {
            return 0;
        }
    }
    return 1;
}
void scope_average_process(struct scope_average *averaging_state,
                           const volatile uint8_t *source,
                           uint8_t *destination,
                           unsigned count,
                           unsigned epoch) {
    unsigned shift = count == 4 ? 2 : count == 16 ? 4 : count == 64 ? 6 : 7;
    unsigned fresh = !averaging_state->initialized || averaging_state->epoch != epoch ||
                     averaging_state->count != count;
    for (unsigned i = 0; i < 4000; i++) {
        int32_t sample = (int32_t)(int8_t)source[i] * 65536;
        if (fresh) {
            averaging_state->value[i] = sample;
        } else {
            int32_t delta = sample - averaging_state->value[i];
            averaging_state->value[i] += delta >= 0 ? delta >> shift : -((-delta) >> shift);
        }
        int value = averaging_state->value[i] >= 0 ? averaging_state->value[i] >> 16
                                                   : -((-averaging_state->value[i]) >> 16);
        destination[i] = (uint8_t)(int8_t)value;
    }
    averaging_state->initialized = 1;
    averaging_state->epoch = epoch;
    averaging_state->count = count;
}
