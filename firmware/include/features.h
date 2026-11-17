#ifndef SCOPE_FEATURES_H
#define SCOPE_FEATURES_H
#include "advanced.h"
#include "controls.h"
struct scope_trigger_bank {
    struct scope_trigger trigger;
    struct scope_advanced_trigger advanced;
};
struct scope_features {
    unsigned bandwidth[2], acquire_mode, average_count, trigger_family;
    struct scope_trigger_bank bank[2], normal;
};
#define SCOPE_BANK_DEFAULT(ch)                                                                     \
    {{ch, 0, 0, 0, 2000000, 100, 0, 0}, {0, 0, 500000, 1000000, 3000000, 2, 1, 0, 0}}
#define SCOPE_FEATURES_DEFAULT                                                                     \
    {{0, 0}, 0, 16, 0, {SCOPE_BANK_DEFAULT(0), SCOPE_BANK_DEFAULT(1)}, SCOPE_BANK_DEFAULT(0)}
void scope_features_defaults(struct scope_features *features_config);
int scope_features_valid(const struct scope_features *features_config);
/* Stock 4e9b8 uses y += (x-y)/N. Q16 preserves sub-code state without soft-float. */
struct scope_average {
    int32_t value[4000];
    unsigned initialized, epoch, count;
};
void scope_average_process(struct scope_average *averaging_state,
                           const volatile uint8_t *source,
                           uint8_t *destination,
                           unsigned count,
                           unsigned epoch);
#endif
