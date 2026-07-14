#include <stdio.h>
#include "lcd_bl_pwm_bsp.h"
#include "esp_err.h"
#include "exio_bsp.h"

// V2: backlight is binary via EXIO bit1. Keep API compatible with original demo.

void gpio_init(void)
{
  // no direct BL GPIO on V2
}

void lcd_bl_pwm_bsp_init(uint16_t duty)
{
  // any non-zero duty => ON
  (void)exio_bsp_set_backlight(duty > 0);
}

void setUpduty(uint16_t duty)
{
  (void)exio_bsp_set_backlight(duty > 0);
}
