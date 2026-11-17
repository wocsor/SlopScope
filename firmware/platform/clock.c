#include "platform.h"

static uint32_t previous_timer_ticks;
static uint32_t elapsed_milliseconds;
static uint32_t fractional_millisecond_ticks;
static unsigned int timer_tracking_initialized;

uint32_t timer_ticks(void) {
    uint32_t tick = MMIO32(0x01c20c28u);
    if (timer_tracking_initialized) {
        uint32_t delta = previous_timer_ticks - tick;
        elapsed_milliseconds += delta / 24000u;
        fractional_millisecond_ticks += delta % 24000u;
        if (fractional_millisecond_ticks >= 24000u) {
            elapsed_milliseconds++;
            fractional_millisecond_ticks -= 24000u;
        }
    } else {
        timer_tracking_initialized = 1;
    }
    previous_timer_ticks = tick;
    return tick;
}

uint32_t timer_milliseconds(void) {
    timer_ticks();
    return elapsed_milliseconds;
}

unsigned timer_elapsed_ms(uint32_t started_ms) {
    return timer_milliseconds() - started_ms;
}

int timer_has_elapsed(uint32_t started_ticks, unsigned duration_ms) {
    return started_ticks - timer_ticks() >= duration_ms * 24000u;
}

void display_delay_ms(unsigned duration_ms) {
    uint32_t started_ticks = timer_ticks();
    while (!timer_has_elapsed(started_ticks, duration_ms)) {
        __asm__ volatile("nop");
    }
}
