#ifndef EXIO_BSP_H
#define EXIO_BSP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize TCA9554: configure RST/BL as outputs, pulse LCD reset, leave RST released, BL off.
bool exio_bsp_init(void);

// Enable/disable backlight (keeps LCD RST released/high).
bool exio_bsp_set_backlight(bool on);

// Pulse LCD reset via EXIO bit5 (active-low assumed).
bool exio_bsp_lcd_reset_pulse(void);

#ifdef __cplusplus
}
#endif

#endif
