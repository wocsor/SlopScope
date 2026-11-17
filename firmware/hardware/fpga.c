#include "scope_internal.h"

void fpga_prepare_gpio(void) {
    gpio_set_function(GPIOF_BASE, 3, 1);
    gpio_set_pull(GPIOF_BASE, 3, 1);
    gpio_write(GPIOF_BASE, 3, 0);
    gpio_set_function(GPIOE_BASE, 11, 1);
    gpio_set_pull(GPIOE_BASE, 11, 1);
    gpio_set_function(GPIOE_BASE, 12, 1);
    gpio_set_pull(GPIOE_BASE, 12, 1);
    gpio_set_function(GPIOF_BASE, 0, 0);
    gpio_set_pull(GPIOF_BASE, 0, 1);
    gpio_set_function(GPIOF_BASE, 1, 1);
    gpio_set_pull(GPIOF_BASE, 1, 1);
    gpio_write(GPIOF_BASE, 1, 1);
    gpio_set_function(GPIOD_BASE, 12, 0);
    gpio_set_pull(GPIOD_BASE, 12, 1);
}

void fpga_configuration_poll(void) {

    if (scope_state.fpga.state == FPGA_CONFIG_HOLD_RESET &&
        timer_has_elapsed(scope_state.fpga.started_ticks, 100)) {
        gpio_write(GPIOF_BASE, 1, 1);
        scope_state.fpga.started_ticks = timer_ticks();
        scope_state.fpga.state = FPGA_CONFIG_WAIT_INIT;
    }
    if (scope_state.fpga.state == FPGA_CONFIG_WAIT_INIT) {
        if (gpio_read(GPIOF_BASE, 0)) {
            scope_state.fpga.state = FPGA_CONFIG_SHIFT_IMAGE;
        } else if (timer_has_elapsed(scope_state.fpga.started_ticks, 1000)) {
            scope_state.fpga.error = 1;
            scope_state.fpga.state = FPGA_CONFIG_FAILED;
        }
    }
    if (scope_state.fpga.state == FPGA_CONFIG_SHIFT_IMAGE) {
        unsigned chunk_end = scope_state.fpga.bytes_sent + 128;
        if (chunk_end > FPGA_CONFIGURATION_BYTES) {
            chunk_end = FPGA_CONFIGURATION_BYTES;
        }
        while (scope_state.fpga.bytes_sent < chunk_end) {
            unsigned configuration_byte = FPGA_CONFIGURATION_IMAGE[scope_state.fpga.bytes_sent++];
            for (unsigned bit = 0; bit < 8; bit++) {
                if (scope_state.fpga.slow_clock) {
                    gpio_write(GPIOE_BASE, 11, 0);
                    gpio_settle();
                    gpio_write(GPIOE_BASE, 12, (configuration_byte & 0x80) != 0);
                    gpio_settle();
                    gpio_write(GPIOE_BASE, 11, 1);
                    gpio_settle();
                } else {
                    gpio_write(GPIOE_BASE, 11, 1);
                    gpio_write(GPIOE_BASE, 12, (configuration_byte & 0x80) != 0);
                    gpio_write(GPIOE_BASE, 11, 0);
                }
                configuration_byte <<= 1;
            }
        }
        if (scope_state.fpga.bytes_sent == FPGA_CONFIGURATION_BYTES) {
            gpio_write(GPIOE_BASE, 11, 1);
            scope_state.fpga.started_ticks = timer_ticks();
            scope_state.fpga.state = FPGA_CONFIG_WAIT_DONE;
        }
    }
    if (scope_state.fpga.state == FPGA_CONFIG_WAIT_DONE &&
        timer_has_elapsed(scope_state.fpga.started_ticks, 10)) {
        if (gpio_read(GPIOD_BASE, 12)) {
            for (unsigned i = 0; i < 41; i++) {
                gpio_write(GPIOE_BASE, 11, 1);
                gpio_write(GPIOE_BASE, 11, 0);
            }
            gpio_set_function(GPIOE_BASE, 11, 0);
            gpio_set_pull(GPIOE_BASE, 11, 1);
            if (spi1_initialize()) {
                scope_state.fpga.error = 3;
                scope_state.fpga.state = FPGA_CONFIG_FAILED;
            } else if (fpga_write_register(0x4d0, 1) || fpga_write_register(0x620, 1)) {
                scope_state.fpga.error = 4;
                scope_state.fpga.state = FPGA_CONFIG_FAILED;
            } else {
                scope_state.fpga.started_ticks = timer_ticks();
                scope_state.fpga.state = FPGA_CONFIG_FINISH_RESET;
            }
        } else if (timer_has_elapsed(scope_state.fpga.started_ticks, 1000)) {
            scope_state.fpga.error = 2;
            scope_state.fpga.state = FPGA_CONFIG_FAILED;
        }
    }
    if (scope_state.fpga.state == FPGA_CONFIG_FINISH_RESET &&
        timer_has_elapsed(scope_state.fpga.started_ticks, 10)) {
        if (fpga_write_register(0x620, 0)) {
            scope_state.fpga.error = 4;
            scope_state.fpga.state = FPGA_CONFIG_FAILED;
        } else {
            scope_state.fpga.state = FPGA_CONFIG_READY;
        }
    }
}
