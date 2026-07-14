# 🎵 music_mvp

> 基于 ESP32-S3 + LVGL v8 的体感音乐交互原型项目

一个将 IMU 动作捕捉、触摸屏交互和 Web 实时监控结合在一起的音乐 MVP（最小可行产品）。通过摇晃、敲击开发板来触发不同的乐器音效，配合 Web 仪表盘实现实时姿态可视化和音频调试。

---

## ✨ 功能特性

| 功能 | 说明 |
|------|------|
| 🖥️ **LVGL 图形界面** | 172×640 竖屏 UI，基于官方 AXS15231B QSPI 面板驱动 |
| 📡 **IMU 实时监控** | 6 轴加速度计 + 陀螺仪数据，50Hz 采样率 Web 可视化 |
| 🎛️ **体感乐器** | 4 种演奏模式：鼓槌 🥁 / 三角铁 🔔 / 沙槌 🪇 / 关闭 |
| 🔊 **音频调试面板** | 网页端 Drum Pad + 440Hz 测试音，键盘快捷键触发 |
| 🧭 **3D 姿态立方体** | 实时渲染设备 3D 朝向，互补滤波姿态解算 |
| 🔌 **Web Serial 通信** | 浏览器直接通过 USB 串口与 ESP32 通信，无需安装 App |
| 📊 **Chart.js 波形图** | 加速度/陀螺仪/运动触发三通道实时折线图 |

---

## 🛠️ 硬件规格

| 组件 | 参数 |
|------|------|
| **主控** | ESP32-S3 |
| **屏幕** | Waveshare Touch-LCD-3.49 V2, 172×640, AXS15231B QSPI 驱动, SPI Mode 3 |
| **触摸** | I2C `0x3B`（GPIO 17 SDA / GPIO 18 SCL） |
| **EXIO** | TCA9554 @ I2C `0x20`（GPIO 47 SDA / GPIO 48 SCL） |
| │ | 　• Bit 1: 背光控制 |
| │ | 　• Bit 5: LCD 硬件复位 |
| **QSPI** | CS=GPIO9, PCLK=GPIO10, DATA0-3=GPIO11-14 |
| **TE** | GPIO 21（撕裂效应同步信号） |
| **RTC** | I2C `0x51` |
| **IMU** | I2C `0x6B`（6 轴加速度计+陀螺仪） |

> ⚠️ **V2 版本注意**：LCD 复位和背光由 TCA9554 EXIO 芯片控制，不同于 V1 版本的直接 GPIO 方式。GPIO8 是 EXIO 中断引脚，不可直接用于背光 PWM。

---

## 📁 项目结构

```
music_mvp/
├── 09_LVGL_V8_Test_V2.ino   # 主程序入口（Arduino/PlatformIO）
├── user_config.h             # 硬件引脚、I2C 地址、屏幕参数配置
├── lvgl_port.h / .c          # LVGL 移植层：显示驱动、触摸驱动、任务调度
├── i2c_bsp.h / .c            # I2C 总线驱动：触摸/RTC/IMU/EXIO 设备管理
├── exio_bsp.h / .c           # TCA9554 EXIO 驱动：LCD 复位、背光控制
├── lv_conf.h                 # LVGL v8 配置文件
├── imu_monitor.html          # Web 仪表盘（IMU 监控 + 音频调试 + 体感乐器）
│
├── src/
│   ├── axs15231b/            # AXS15231B 面板驱动（QSPI/SPI/I80 多模式支持）
│   │   ├── esp_lcd_axs15231b.h
│   │   └── esp_lcd_axs15231b.c
│   ├── touch/                # 触摸屏抽象层
│   │   ├── esp_lcd_touch.h
│   │   └── esp_lcd_touch.c
│   └── lcd_bl_bsp/           # 背光 PWM 驱动（V1 兼容，V2 使用 EXIO）
│       ├── lcd_bl_pwm_bsp.h
│       └── lcd_bl_pwm_bsp.c
│
└── demos/                    # LVGL 示例程序（当前为空）
```

---

## 🚀 快速开始

### 1. 环境准备

- **Arduino IDE** 或 **PlatformIO**，安装 ESP32-S3 开发板支持
- 安装以下 Arduino 库：
  - `lvgl` (v8.x)
  - ESP32 官方 BSP / LCD 驱动库

### 2. 编译 & 烧录

```bash
# PlatformIO
pio run --target upload

# 或 Arduino IDE 中直接打开 .ino 文件编译上传
```

烧录后，屏幕将显示蓝色背景的 LVGL 诊断界面，确认面板驱动正常工作。

### 3. 打开 Web 仪表盘

1. 用 Chrome/Edge 浏览器打开 `imu_monitor.html`
2. 点击 **Connect** 按钮，在弹出列表中选择 ESP32-S3 串口设备
3. 连接成功后即可看到实时 IMU 数据流

---

## 🎮 使用说明

### Web 仪表盘标签页

| 标签 | 功能 |
|------|------|
| **📊 IMU Monitor** | 加速度/陀螺仪实时波形、3D 姿态立方体、原始数据日志 |
| **🔊 Audio Debug** | 网页打击垫（Kick/Snare/Hat/Tom）、音量滑块、测试音 |
| **🎵 Instrument** | 体感乐器模式选择、运动触发阈值可视化 |

### 体感乐器模式

通过串口发送 `M<n>` 命令或点击仪表盘切换模式：

| 模式 | 命令 | 触发方式 |
|------|------|----------|
| 🥁 鼓槌 | `M0` | 快速敲击动作触发鼓声 |
| 🔔 三角铁 | `M1` | 轻敲触发三角铁音效 |
| 🪇 沙槌 | `M2` | 连续摇晃触发沙槌音效 |
| ⏹️ 关闭 | `M3` | 停止体感触发 |

### 键盘快捷键（Audio Debug 标签页）

| 按键 | 乐器 |
|------|------|
| `Q` | 🥁 Kick |
| `W` | 🔔 Snare |
| `E` | 💿 Hat |
| `R` | 🪘 Tom |

---

## 🔧 串口通信协议

Web 仪表盘与 ESP32 之间通过 115200bps 串口通信：

### 上行（PC → ESP32）

| 命令 | 说明 |
|------|------|
| `M0` ~ `M3` | 切换体感乐器模式 |
| `K` / `S` / `H` / `T` | 触发 Kick/Snare/Hat/Tom 鼓声 |
| `A` | 发送 440Hz 测试音 |
| `V<0-100>` | 设置音量百分比 |

### 下行（ESP32 → PC）

```
ax,ay,az,gx,gy,gz
```
6 个浮点数值的 CSV 格式，对应加速度计 X/Y/Z（单位 g）和陀螺仪 X/Y/Z（单位 °/s）。

---

## 📝 配置说明

`user_config.h` 中的关键宏定义：

```c
// 软件旋转：0 = 原生 172×640 竖屏, 1 = 旋转 90° 为 640×172
#define Rotated USER_DISP_ROT_NONO

// I2C 地址
#define EXAMPLE_EXIO_ADDR   0x20   // TCA9554
#define EXAMPLE_RTC_ADDR    0x51   // RTC
#define EXAMPLE_IMU_ADDR    0x6B   // IMU
#define I2C_TOUCH_ADDR      0x3B   // 触摸屏

// 背光测试模式
#define Backlight_Testing   0
```

---

## 🏗️ 架构设计

```
┌─────────────────────────────────────────────┐
│                  Arduino Sketch              │
│           09_LVGL_V8_Test_V2.ino             │
│  setup() → i2c_init → lvgl_init → backlight │
│  loop()  → delay(1000)  [LVGL on Core 0]    │
└──────────┬──────────────┬───────────────────┘
           │              │
    ┌──────▼──────┐  ┌───▼───────────────────┐
    │  lvgl_port  │  │     i2c_bsp           │
    │  LVGL v8    │  │  port0: RTC/IMU/EXIO  │
    │  Display    │  │  port1: Touch          │
    │  Touch      │  └───────────────────────┘
    │  Tick       │
    └──────┬──────┘
           │
    ┌──────▼──────────────────────────────┐
    │     esp_lcd_axs15231b (QSPI)        │
    │  172×640 RGB565, full_refresh=1     │
    └─────────────────────────────────────┘
```

- **LVGL 任务**运行在 FreeRTOS Core 0，使用双缓冲（SPIRAM）+ DMA
- **I2C** 双总线架构：port0 承载 RTC/IMU/EXIO，port1 独立承载触摸屏
- **面板刷新**采用分段 DMA 传输 + 信号量同步，避免撕裂

---

## 📄 许可证

本项目中的 ESP-IDF 官方驱动文件（`src/` 目录下 ESP LCD/Touch 相关代码）遵循 **Apache-2.0** 许可证，版权归 Espressif Systems 所有。

其余自定义代码可根据需要自行确定许可证。

---

## ⚠️ 注意事项

1. **V2 硬件**的 LCD 复位和背光由 TCA9554 EXIO 控制，务必确认 I2C 地址为 `0x20`
2. 屏幕 `full_refresh` 必须设为 `1`，否则 QSPI 面板会出现撕裂
3. LVGL tick 周期为 5ms，LVGL 任务栈大小 4000 字
4. IMU 数据通过 Web Serial API 读取，仅支持 Chrome/Edge 等 Chromium 内核浏览器
5. `demos/` 目录当前为空，LVGL 官方 demo 组件未编译进固件以节省空间
