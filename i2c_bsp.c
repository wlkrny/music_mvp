#include <stdio.h>
#include <stdbool.h>
#include "i2c_bsp.h"
#include "user_config.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

static const char *I2C_TAG = "i2c_bsp";

static i2c_master_bus_handle_t user_i2c_port0_handle = NULL;
static i2c_master_bus_handle_t user_i2c_port1_handle = NULL;
i2c_master_dev_handle_t disp_touch_dev_handle = NULL;
i2c_master_dev_handle_t rtc_dev_handle = NULL;
i2c_master_dev_handle_t imu_dev_handle = NULL;
static i2c_master_dev_handle_t exio_dev_handle = NULL;


static uint32_t i2c_data_pdMS_TICKS = 0;
static uint32_t i2c_done_pdMS_TICKS = 0;


void i2c_master_Init(void)
{
  i2c_data_pdMS_TICKS = pdMS_TO_TICKS(5000);
  i2c_done_pdMS_TICKS = pdMS_TO_TICKS(1000);
  /*i2c_port 0 init*/
  i2c_master_bus_config_t i2c_bus_config = 
  {
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .i2c_port = I2C_NUM_0,
    .scl_io_num = ESP_SCL_NUM,
    .sda_io_num = ESP_SDA_NUM,
    .glitch_ignore_cnt = 7,
    .flags = {
      .enable_internal_pullup = true,
    },
  };
  ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &user_i2c_port0_handle));
  i2c_bus_config.scl_io_num = Touch_SCL_NUM;
  i2c_bus_config.sda_io_num = Touch_SDA_NUM;
  i2c_bus_config.i2c_port = I2C_NUM_1;
  ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &user_i2c_port1_handle));
  
  i2c_device_config_t dev_cfg = 
  {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .scl_speed_hz = 300000,
  };
  dev_cfg.device_address = EXAMPLE_RTC_ADDR;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(user_i2c_port0_handle, &dev_cfg, &rtc_dev_handle));

  dev_cfg.device_address = EXAMPLE_IMU_ADDR;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(user_i2c_port0_handle, &dev_cfg, &imu_dev_handle));

  dev_cfg.device_address = I2C_TOUCH_ADDR;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(user_i2c_port1_handle, &dev_cfg, &disp_touch_dev_handle));

  // V2 EXIO TCA9554 on ESP bus (try common addresses 0x20 then 0x27)
  {
    uint8_t candidates[2] = { EXAMPLE_EXIO_ADDR, 0x27 };
    bool found = false;
    for (int i = 0; i < 2; i++) {
      esp_err_t pr = i2c_master_probe(user_i2c_port0_handle, candidates[i], 200);
      if (pr == ESP_OK) {
        dev_cfg.device_address = candidates[i];
        if (i2c_master_bus_add_device(user_i2c_port0_handle, &dev_cfg, &exio_dev_handle) == ESP_OK) {
          ESP_LOGI(I2C_TAG, "EXIO found at 0x%02X", candidates[i]);
          found = true;
          break;
        }
      }
    }
    if (!found) {
      // still register configured address so later writes surface clear errors
      dev_cfg.device_address = EXAMPLE_EXIO_ADDR;
      if (i2c_master_bus_add_device(user_i2c_port0_handle, &dev_cfg, &exio_dev_handle) != ESP_OK) {
        ESP_LOGW(I2C_TAG, "EXIO device add failed at 0x%02X", EXAMPLE_EXIO_ADDR);
        exio_dev_handle = NULL;
      } else {
        ESP_LOGW(I2C_TAG, "EXIO not probed; using configured 0x%02X", EXAMPLE_EXIO_ADDR);
      }
    }
  }

}

bool i2c_exio_ensure(void)
{
  return exio_dev_handle != NULL;
}

bool i2c_exio_write_reg(uint8_t reg, uint8_t val)
{
  if (exio_dev_handle == NULL) {
    return false;
  }
  uint8_t buf[2] = { reg, val };
  // wait on port0
  if (i2c_master_bus_wait_all_done(user_i2c_port0_handle, i2c_done_pdMS_TICKS) != ESP_OK) {
    return false;
  }
  return i2c_master_transmit(exio_dev_handle, buf, 2, i2c_data_pdMS_TICKS) == ESP_OK;
}

uint8_t i2c_write_buff(i2c_master_dev_handle_t dev_handle,int reg,uint8_t *buf,uint8_t len)
{
  uint8_t ret;
  uint8_t *pbuf = NULL;
  ret = i2c_master_bus_wait_all_done(user_i2c_port0_handle,i2c_done_pdMS_TICKS);
  if(ret != ESP_OK)
  return ret;
  if(reg == -1)
  {
    ret = i2c_master_transmit(dev_handle,buf,len,i2c_data_pdMS_TICKS);
  }
  else
  {
    pbuf = (uint8_t*)malloc(len+1);
    pbuf[0] = reg;
    for(uint8_t i = 0; i<len; i++)
    {
      pbuf[i+1] = buf[i];
    }
    ret = i2c_master_transmit(dev_handle,pbuf,len+1,i2c_data_pdMS_TICKS);
    free(pbuf);
    pbuf = NULL;
  }
  return ret;
}
uint8_t i2c_master_write_read_dev(i2c_master_dev_handle_t dev_handle,uint8_t *writeBuf,uint8_t writeLen,uint8_t *readBuf,uint8_t readLen)
{
  uint8_t ret;
  ret = i2c_master_bus_wait_all_done(user_i2c_port0_handle,i2c_done_pdMS_TICKS);
  if(ret != ESP_OK)
  return ret;
  ret = i2c_master_transmit_receive(dev_handle,writeBuf,writeLen,readBuf,readLen,i2c_data_pdMS_TICKS);
  return ret;
}
uint8_t i2c_read_buff(i2c_master_dev_handle_t dev_handle,int reg,uint8_t *buf,uint8_t len)
{
  uint8_t ret;
  uint8_t addr = 0;
  ret = i2c_master_bus_wait_all_done(user_i2c_port0_handle,i2c_done_pdMS_TICKS);
  if(ret != ESP_OK)
  return ret;
  if( reg == -1 )
  {ret = i2c_master_receive(dev_handle, buf,len, i2c_data_pdMS_TICKS);}
  else
  {addr = (uint8_t)reg; ret = i2c_master_transmit_receive(dev_handle,&addr,1,buf,len,i2c_data_pdMS_TICKS);}
  return ret;
}


uint8_t i2c_master_touch_write_read(i2c_master_dev_handle_t dev_handle,uint8_t *writeBuf,uint8_t writeLen,uint8_t *readBuf,uint8_t readLen)
{
  uint8_t ret;
  ret = i2c_master_bus_wait_all_done(user_i2c_port1_handle,i2c_done_pdMS_TICKS);
  if(ret != ESP_OK)
  return ret;
  ret = i2c_master_transmit_receive(dev_handle,writeBuf,writeLen,readBuf,readLen,i2c_data_pdMS_TICKS);
  return ret;
}
