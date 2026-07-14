# 09_LVGL_V8_Test_V2

基于微雪官方 `09_LVGL_V8_Test` 的 **Touch-LCD-3.49 V2 适配版**。

## 相对原版官方 demo 的改动

| 项 | 原版 demo | V2 适配 |
|---|---|---|
| LCD RST | GPIO21 | TCA9554 EXIO **bit5** |
| 背光 | GPIO8 PWM | EXIO **bit1** |
| QSPI 9–14 / 触摸 I2C | 同 | 同 |
| 面板驱动 | AXS15231B mode3 + esp_lcd | 保留官方路径 |
| UI | `lv_demo_widgets`（Arduino 库默认不编进 demos） | 诊断 UI：蓝底 + 绿条 + 红条 + 白字 |

## 期望画面

- 深蓝背景  
- 顶部绿色横条  
- 中间白字：`Waveshare 3.49 V2 / Official AXS15231B path / EXIO RST/BL / 172x640 OK?`  
- 底部红色横条  

若仍是随机花屏 → 面板 init / EXIO 地址仍需再调，但 pin 已按 V2。

## 烧录

**下载模式（重要）：按住 BOOT → 点一下 EN → 松开 BOOT**

```bash
FQBN='esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,PSRAM=opi,FlashSize=16M,FlashMode=qio,PartitionScheme=app3M_fat9M_16MB'

arduino-cli compile --upload \
  -p /dev/cu.usbmodemXXXX \
  --fqbn "$FQBN" \
  --build-property "compiler.c.extra_flags=-DBOARD_HAS_PSRAM" \
  --build-property "compiler.cpp.extra_flags=-DBOARD_HAS_PSRAM" \
  .
```

说明：
- 第一次自动上传用了 `USBMode=default`（TinyUSB），可能让 `303A:1001` JTAG 串口一时失效  
- 推荐后续用 `USBMode=hwcdc`（与当前 USB-Serial-JTAG 端口一致）  
- 若连不上：只能 **BOOT+EN** 再刷
