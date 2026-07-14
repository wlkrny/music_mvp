# music_mvp

ESP32-S3 LVGL project for Waveshare Touch-LCD-3.49 V2.

## Board

- MCU: ESP32-S3
- Display: Waveshare Touch-LCD-3.49 V2, 172x640, AXS15231B QSPI driver, mode 3
- Touch: I2C 0x3B (GPIO 17/18)
- EXIO: TCA9554 at I2C 0x20 (GPIO 47/48)
  - Bit 1: backlight
  - Bit 5: LCD reset
- QSPI: GPIO 9 (CS), 10 (PCLK), 11-14 (DATA0-3)
- TE: GPIO 21
- RTC: I2C 0x51
- IMU: I2C 0x6B
