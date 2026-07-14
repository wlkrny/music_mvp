#include "user_config.h"
#include "lvgl_port.h"
#include "esp_err.h"
#include "i2c_bsp.h"
#include "exio_bsp.h"

// V2-adapted Waveshare 09_LVGL_V8_Test
// - Uses AXS15231B official QSPI panel driver (mode 3)
// - LCD reset + backlight via TCA9554 EXIO (not GPIO21/8)

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println(F("=== 09_LVGL_V8_Test_V2 (Waveshare 3.49 V2) ==="));
  Serial.println(F("Panel: AXS15231B QSPI 172x640"));
  Serial.println(F("RST/BL: TCA9554 EXIO bit5/bit1"));

  i2c_master_Init();
  Serial.println(F("[setup] I2C ready"));

  lvgl_port_init();
  Serial.println(F("[setup] LVGL/panel ready"));

  // backlight after first frames are set up
  if (exio_bsp_set_backlight(true)) {
    Serial.println(F("[setup] Backlight ON via EXIO"));
  } else {
    Serial.println(F("[setup] Backlight EXIO write failed"));
  }

  Serial.println(F("[setup] Done. Expect LVGL widgets demo on LCD."));
}

void loop()
{
  // LVGL task runs on FreeRTOS core 0
  delay(1000);
}
