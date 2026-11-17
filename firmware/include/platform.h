#ifndef SCOPE_PLATFORM_H
#define SCOPE_PLATFORM_H
#include "board.h"
#include <stdint.h>
uint32_t timer_ticks(void);
uint32_t timer_milliseconds(void);
unsigned timer_elapsed_ms(uint32_t started_ms);
int timer_has_elapsed(uint32_t started_ticks, unsigned duration_ms);
void display_delay_ms(unsigned duration_ms);
void gpio_set_function(unsigned port_base, unsigned pin, unsigned function);
void gpio_set_pull(unsigned port_base, unsigned pin, unsigned pull_mode);
void gpio_write(unsigned port_base, unsigned pin, unsigned is_high);
unsigned gpio_read(unsigned port_base, unsigned pin);
void gpio_settle(void);
#endif
