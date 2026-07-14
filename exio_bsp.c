#include "exio_bsp.h"
#include "user_config.h"
#include "i2c_bsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "exio_bsp";

// Raw 7-bit writes on ESP I2C bus (port0, SDA47/SCL48)
// Uses existing i2c_master handles via i2c_write_buff if a device was added,
// otherwise temporary transmissions through the EXIO device handle.
extern void *user_i2c_get_port0_bus(void); // optional if we export later

// Local device handle for EXIO

// We need to register EXIO after i2c_master_Init(). Provide a prepare API.
// i2c_bsp only exposes touch/rtc/imu. Add path using public i2c API:
// Re-open by adding device in this module — but we need bus handle.
// Workaround: implement lightweight write using i2c_master_probe-less transmit
// by hijacking rtc_dev_handle bus? Better: extend i2c_bsp.
// For portability we duplicate a small init that expects i2c_master_Init already called
// and uses i2c_write-like linkage after we add EXIO device inside i2c_bsp.
//
// Here we call helpers declared in i2c_bsp extensions.

// Implemented in i2c_bsp.c:
bool i2c_exio_write_reg(uint8_t reg, uint8_t val);
bool i2c_exio_ready(void);
bool i2c_exio_ensure(void);

bool exio_bsp_init(void)
{
  if (!i2c_exio_ensure()) {
    ESP_LOGE(TAG, "EXIO device not ready at 0x%02X", EXAMPLE_EXIO_ADDR);
    return false;
  }

  // Config reg: bit1(BL), bit5(RST) = outputs (0), others inputs (1)
  uint8_t cfg = (uint8_t)(0xFF & ~((1u << EXAMPLE_EXIO_BIT_BL_EN) | (1u << EXAMPLE_EXIO_BIT_LCD_RST)));
  // => 0xDD
  if (!i2c_exio_write_reg(EXAMPLE_EXIO_REG_CONFIG, cfg)) {
    ESP_LOGE(TAG, "exio config write failed");
    return false;
  }

  // Output: RST low (assert), BL off
  if (!i2c_exio_write_reg(EXAMPLE_EXIO_REG_OUTPUT, 0x00)) {
    ESP_LOGE(TAG, "exio output write failed");
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(20));

  // Release RST (bit5=1), BL still off
  if (!i2c_exio_write_reg(EXAMPLE_EXIO_REG_OUTPUT, (uint8_t)(1u << EXAMPLE_EXIO_BIT_LCD_RST))) {
    ESP_LOGE(TAG, "exio reset release failed");
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(50));

  ESP_LOGI(TAG, "EXIO OK addr=0x%02X cfg=0x%02X (RST released, BL off)", EXAMPLE_EXIO_ADDR, cfg);
  return true;
}

bool exio_bsp_lcd_reset_pulse(void)
{
  if (!i2c_exio_ensure()) return false;

  // assert reset: bit5=0, bl off
  if (!i2c_exio_write_reg(EXAMPLE_EXIO_REG_OUTPUT, 0x00)) return false;
  vTaskDelay(pdMS_TO_TICKS(30));
  // keep reset a bit longer like official demo
  if (!i2c_exio_write_reg(EXAMPLE_EXIO_REG_OUTPUT, 0x00)) return false;
  vTaskDelay(pdMS_TO_TICKS(250));
  // release RST
  if (!i2c_exio_write_reg(EXAMPLE_EXIO_REG_OUTPUT, (uint8_t)(1u << EXAMPLE_EXIO_BIT_LCD_RST))) return false;
  vTaskDelay(pdMS_TO_TICKS(30));
  ESP_LOGI(TAG, "LCD reset pulse done via EXIO");
  return true;
}

bool exio_bsp_set_backlight(bool on)
{
  if (!i2c_exio_ensure()) return false;
  uint8_t val = (uint8_t)(1u << EXAMPLE_EXIO_BIT_LCD_RST); // keep RST released
  if (on) val |= (uint8_t)(1u << EXAMPLE_EXIO_BIT_BL_EN);
  if (!i2c_exio_write_reg(EXAMPLE_EXIO_REG_OUTPUT, val)) {
    ESP_LOGE(TAG, "backlight write failed");
    return false;
  }
  ESP_LOGI(TAG, "backlight %s", on ? "ON" : "OFF");
  return true;
}
