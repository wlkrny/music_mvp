#ifndef USER_CONFIG_H
#define USER_CONFIG_H

// ============================================================
// Waveshare ESP32-S3-Touch-LCD-3.49 **V2** adapted demo
// Based on official 09_LVGL_V8_Test + V2 EXIO (RST/BL)
// ============================================================

//spi & i2c handle
#define LCD_HOST SPI3_HOST

// touch I2C port
#define Touch_SCL_NUM (GPIO_NUM_18)
#define Touch_SDA_NUM (GPIO_NUM_17)

// touch esp
#define ESP_SCL_NUM (GPIO_NUM_48)
#define ESP_SDA_NUM (GPIO_NUM_47)

//  DISP QSPI (same on V1/V2)
#define EXAMPLE_PIN_NUM_LCD_CS     (GPIO_NUM_9)
#define EXAMPLE_PIN_NUM_LCD_PCLK   (GPIO_NUM_10)
#define EXAMPLE_PIN_NUM_LCD_DATA0  (GPIO_NUM_11)
#define EXAMPLE_PIN_NUM_LCD_DATA1  (GPIO_NUM_12)
#define EXAMPLE_PIN_NUM_LCD_DATA2  (GPIO_NUM_13)
#define EXAMPLE_PIN_NUM_LCD_DATA3  (GPIO_NUM_14)

// V2: LCD_RST is NOT a direct GPIO. Use TCA9554 EXIO bit5.
// GPIO21 is TE (tear-effect). Do NOT drive as RST.
#define EXAMPLE_PIN_NUM_LCD_RST    (-1)
#define EXAMPLE_PIN_NUM_LCD_TE     (GPIO_NUM_21)

// V2: backlight is EXIO bit1, NOT GPIO8 PWM.
// GPIO8 is EXIO_INT. Keep pin define only for non-V2 reference.
#define EXAMPLE_PIN_NUM_BK_LIGHT   (-1)

// TCA9554 EXIO (I2C on ESP bus 47/48)
// Music_mvp scan found 0x20 (header remnants may say 0x27)
#define EXAMPLE_EXIO_ADDR          0x20
#define EXAMPLE_EXIO_BIT_BL_EN     1
#define EXAMPLE_EXIO_BIT_LCD_RST   5
// TCA9554 regs
#define EXAMPLE_EXIO_REG_OUTPUT    0x01
#define EXAMPLE_EXIO_REG_CONFIG    0x03

#define I2C_TOUCH_ADDR                    0x3b
#define EXAMPLE_PIN_NUM_TOUCH_RST         (-1)
#define EXAMPLE_PIN_NUM_TOUCH_INT         (-1)


#define EXAMPLE_LVGL_TICK_PERIOD_MS    5
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 5


/*bl test*/
#define Backlight_Testing 0

/*ADDR*/
#define EXAMPLE_RTC_ADDR 0x51
#define EXAMPLE_IMU_ADDR 0x6b


#define USER_DISP_ROT_90    1
#define USER_DISP_ROT_NONO  0
#define Rotated USER_DISP_ROT_NONO   // 软件旋转: 关闭 = native 172x640


#if (Rotated == USER_DISP_ROT_NONO)
#define EXAMPLE_LCD_H_RES 172
#define EXAMPLE_LCD_V_RES 640
#else
#define EXAMPLE_LCD_H_RES 640
#define EXAMPLE_LCD_V_RES 172
#endif

#define LCD_NOROT_HRES     172
#define LCD_NOROT_VRES     640
#define LVGL_DMA_BUFF_LEN (LCD_NOROT_HRES * 64 * 2)
#define LVGL_SPIRAM_BUFF_LEN (EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * 2)

#endif
