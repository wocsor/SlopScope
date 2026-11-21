#ifndef SCOPE_BOARD_H
#define SCOPE_BOARD_H
#include <stdint.h>
#define MMIO32(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define MMIO16(address) (*(volatile uint16_t *)(uintptr_t)(address))
#define MMIO8(address) (*(volatile uint8_t *)(uintptr_t)(address))
#define SPI1_BASE 0x01c06000u
#define GPIOA_BASE 0x01c20800u
#define GPIOD_BASE 0x01c2086cu
#define GPIOE_BASE 0x01c20890u
#define GPIOF_BASE 0x01c208b4u
#define FPGA_CONFIGURATION_IMAGE ((const uint8_t *)0x817230c0u)
#define FPGA_CONFIGURATION_BYTES 636678u
#define CSI_BASE 0x01cb0000u
#define CSI_SAMPLE_BUFFER ((volatile uint8_t *)0x81000000u)
#define RECORD_BUFFER_BASE ((uint32_t *)0x81010000u)
#define DISPLAY_FRAMEBUFFER_0 0x81c00000u
#define DISPLAY_FRAMEBUFFER_1 0x81cc0000u
#define DISPLAY_PLOT_LEFT 5
#define DISPLAY_PLOT_TOP 40
#define DISPLAY_PLOT_WIDTH 750
#define DISPLAY_PLOT_HEIGHT 400
#define DISPLAY_GRID_COLUMNS 15
#define SCOPE_RATE_COUNT 32u
#endif
