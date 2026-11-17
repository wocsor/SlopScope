#include "scope_internal.h"

struct scope_runtime_state scope_state;

void scope_initialize(void) {
    scope_state.capture.rate_index = 1;
    scope_state.capture.normal_mode = 1;
    scope_state.capture.stream_compression = 1;
    scope_state.capture.roll_live = 1;
    scope_state.frame.valid_count = 1998;
    scope_state.configuration.features = (struct scope_features)SCOPE_FEATURES_DEFAULT;
    scope_state.trigger.configured = (struct scope_trigger){0, 0, 0, 0, 2000000, 100, 0, 0};
    scope_state.configuration.acquisition = (struct scope_acquisition_control){10000, 0, 0};
    scope_state.trigger.advanced =
        (struct scope_advanced_trigger){0, 0, 500000, 1000000, 3000000, 2, 1, 0, 0};
    scope_state.channels.probe_multiplier[0] = 1;
    scope_state.channels.probe_multiplier[1] = 1;
    scope_state.channels.visible[0] = 1;
    scope_state.channels.visible[1] = 1;
    scope_state.display.view_points = 750;
    scope_state.channels.range[0] = 9;
    scope_state.channels.range[1] = 9;
    scope_state.channels.coupling[0] = 0;
    scope_state.channels.coupling[1] = 0;
    scope_state.channels.offset_dac[0] = 30445;
    scope_state.channels.offset_dac[1] = 30552;
    scope_state.frame.legacy_alignment = 2;
    scope_state.frame.channel_alignment = 2;
    scope_state.display.display_redraw_requested = 1;
    scope_state.display.draw_framebuffer = (volatile uint16_t *)DISPLAY_FRAMEBUFFER_1;
    scope_state.settings.save_delay = 2000;
}
