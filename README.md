# music_mvp

> ESP32-S3 体感音乐交互原型 - IMU 手势触发 + 软件鼓合成 + I2S 音频输出

通过摇晃、敲击开发板触发不同的乐器音效，合成音频通过 ES8311 编解码器经 I2S 输出到扬声器。配套 Web 仪表盘实现 IMU 实时可视化和音频调试。

---

## 功能特性

| 功能 | 说明 |
|------|------|
| **IMU 实时数据流** | QMI8658 6 轴传感器, 50Hz CSV 输出到串口 |
| **体感乐器** | 4 种演奏模式：鼓槌 / 三角铁 / 沙槌 / 关闭, 力度感应 |
| **软件鼓合成** | Kick / Snare / Hat / Tom 四通道, 正弦+噪声合成 |
| **I2S 音频输出** | ES8311 DAC 编解码器, 16kHz 立体声, TCA9554 PA 控制 |
| **Web 仪表盘** | Chart.js 实时波形, 3D 姿态立方体, 打击垫, 模式切换 |
| **Web Serial 通信** | 浏览器 USB 串口直连, 无需安装 App |
| **ESP-IDF 组件** | 集成 codec_board + esp_codec_dev 官方音频框架 |

---

## 硬件规格

| 组件 | 参数 |
|------|------|
| **主控** | ESP32-S3 |
| **屏幕** | Waveshare Touch-LCD-3.49 V2, 172x640, AXS15231B (当前固件未启用显示) |
| **音频 DAC** | ES8311 @ I2C 0x18, I2S 输出 (MCLK=7, BCLK=15, WS=46, DOUT=45) |
| **音频 ADC** | ES7210 @ I2C 0x40, I2S 输入 (DIN=6), 预留麦克风 |
| **PA 功放** | TCA9554 @ I2C 0x20, bit7 控制 |
| **IMU** | QMI8658 @ I2C 0x6B, 6 轴 (加速度计 ±8G + 陀螺仪 ±512dps) |
| **I2C 总线** | SDA=GPIO47, SCL=GPIO48 (共享 ES8311/ES7210/QMI8658/TCA9554) |
| **QSPI 显示** | CS=GPIO9, PCLK=GPIO10, DATA0-3=GPIO11-14 (预留, 当前未初始化) |

---

## 项目结构

```
music_mvp/
├── music_mvp.ino              # 主固件 (Arduino/PlatformIO)
├── user_config.h              # 引脚、I2C 地址、显示参数配置
├── imu_monitor.html           # Web 仪表盘
├── README.md
│
├── src/
│   ├── codec_board/           # ESP-IDF 音频板级支持包
│   │   ├── codec_board.h/c    # 板级抽象 (I2C/I2S/Codec/LCD 配置)
│   │   ├── codec_init.h/c     # 编解码器初始化
│   │   ├── board_cfg.h/txt    # 板级引脚配置描述
│   │   ├── cfg_parse.c        # 配置解析器
│   │   ├── drv/tca9554.c/h    # TCA9554 GPIO 扩展驱动
│   │   ├── lcd_init.c         # LCD 初始化 (预留)
│   │   └── dummy_codec.c/h    # 哑编解码器 (纯 I2S)
│   │
│   └── esp_codec_dev/         # ESP-IDF 音频编解码器设备框架
│       ├── esp_codec_dev.c    # 设备管理核心
│       ├── esp_codec_dev_vol.c # 软件音量控制
│       ├── device/es8311/     # ES8311 DAC 驱动
│       ├── device/es7210/     # ES7210 ADC 驱动
│       ├── platform/          # I2C/SPI/I2S/GPIO 平台抽象
│       └── interface/         # 编解码器接口定义
│
└── build/                     # 编译产物 (.gitignore 已排除)
```

---

## 串口通信协议

Web 仪表盘与 ESP32 之间通过 **115200bps** 串口通信。

### 上行 (PC -> ESP32)

| 命令 | 说明 |
|------|------|
| `K` / `S` / `H` / `T` | 触发 Kick / Snare / Hat / Tom 鼓声 |
| `M0` ~ `M3` | 切换乐器模式 (0=鼓槌, 1=三角铁, 2=沙槌, 3=关闭) |
| `V0` ~ `V100` | 设置音量百分比 |

### 下行 (ESP32 -> PC)

```
ax,ay,az,gx,gy,gz
```
6 个浮点数值的 CSV 格式，50Hz 输出：
- `ax,ay,az` - 加速度计 X/Y/Z (单位 m/s^2, ±8G 量程)
- `gx,gy,gz` - 陀螺仪 X/Y/Z (单位 deg/s, ±512dps 量程)

---

## 乐器模式说明

固件通过检测 IMU 加速度幅值的边沿变化来触发音效：

| 模式 | 命令 | 触发阈值 | 冷却 | 音效 |
|------|------|---------|------|------|
| 鼓槌 | `M0` | >15 m/s^2 | 250ms | 力度控制随机鼓声 (K/S/H/T) |
| 三角铁 | `M1` | >12 m/s^2 | 150ms | 2000Hz Ding 衰减音 |
| 沙槌 | `M2` | >12 m/s^2 | 150ms | 白噪声 Shake 衰减音 |
| 关闭 | `M3` | - | - | 仅输出 IMU 数据 |

力度感应：峰值加速度映射到音量因子 0.3~1.0，控制合成振幅。

---

## 快速开始

### 1. 环境准备

- **Arduino IDE** 或 **PlatformIO**，安装 ESP32-S3 (esp32 >= 3.0) 开发板支持
- 安装 ESP-IDF 音频组件 (codec_board, esp_codec_dev 已包含在 `src/` 中)

### 2. 编译与烧录

```bash
# PlatformIO
pio run --target upload

# 或 Arduino IDE 中打开 music_mvp.ino 编译上传
```

串口监视器 (115200bps) 将显示初始化日志：
```
╔═══════════════════════════════════════╗
║  music_mvp v1.0                      ║
║  IMU @50Hz + Drum Pad               ║
╚═══════════════════════════════════════╝
[INIT] i2c_new_master_bus: ret=0
[PA] Speaker amp ON
[AUDIO] Codec OK
[AUDIO] Playback open (16kHz stereo)
[IMU] QMI8658 initialized OK
[MAIN] IMU streaming started
[MAIN] === READY ===
```

### 3. 打开 Web 仪表盘

1. 用 Chrome/Edge 浏览器打开 `imu_monitor.html`
2. 点击 **Connect** 按钮，选择 ESP32-S3 串口设备
3. 连接后可查看实时 IMU 波形、切换乐器模式、触发鼓声

---

## 音频合成算法

软件合成全部在 ESP32-S3 上实时计算，无需外部音频文件：

| 音色 | 合成方法 | 参数 |
|------|---------|------|
| **Kick** | 80Hz 正弦 + 1kHz 短脉冲起音, 指数衰减 (exp(-12t)) | 4096 采样 |
| **Snare** | 180Hz 正弦 + 白噪声混合 (70/30), 指数衰减 (exp(-8t)) | 4096 采样 |
| **Hat** | 纯白噪声, 快速指数衰减 (exp(-25t)) | 4096 采样 |
| **Tom** | 200Hz 正弦 + 频率滑降 (0.7x), 指数衰减 (exp(-5t)) | 4096 采样 |
| **Triangle** | 2000Hz 正弦, 衰减 (exp(-80t)) | 800 采样 (~50ms) |
| **Maraca** | 白噪声, 衰减 (exp(-60t)) | 640 采样 (~40ms) |

输出格式: 16kHz, 16bit, 立体声交错 (I2S standard 格式)。

---

## 架构设计

```
                        Serial (115200)
  PC Browser  <═══════════════════════════>  ESP32-S3
  imu_monitor     CSV IMU data (50Hz)          │
  .html          K/S/H/T drum triggers         │
                  M0-M3 mode switch            │
                  V0-V100 volume               │
                                               │
                    ┌──────────────────────────┤
                    │    music_mvp.ino         │
                    │                          │
         ┌──────────┤  setup()                 │
         │          │    i2c_preinit           │
         │          │    pa_init (TCA9554)     │
         │          │    audio_init (ES8311)   │
         │          │    qmi8658_begin         │
         │          │                          │
         │          │  loop()                  │
         │          │    Serial command parser │
         │          │                          │
         │          │  imu_task (Core 1, 50Hz) │
         │          │    QMI8658 read          │
         │          │    Gesture detection     │
         │          │    Drum synthesis        │
         └──────────┤                          │
                    └──────────────────────────┤
                                               │
              ┌────────────────────────────────┤
              │         I2C Bus (SDA47/SCL48)  │
     ┌────────┼────────┬──────────┬───────────┤
     │        │        │          │           │
  TCA9554  ES8311   ES7210    QMI8658        │
  (PA ctrl) (DAC)   (ADC)     (IMU)          │
     │        │                                   
     │    I2S (MCLK/BCLK/WS/DOUT/DIN)             
     │        │                                   
   Speaker  Codec                                 
```

- **IMU 任务**运行在 FreeRTOS Core 1，50Hz 定时采样并输出 CSV
- **loop()** 运行在 Core 0，处理串口命令解析
- **音频合成**在 IMU 任务中直接写入 I2S 缓冲区，无需额外任务
- **手势检测**使用边沿检测状态机：空闲 -> 挥动峰值追踪 -> 触发 -> 冷却

---

## 许可证

- `src/codec_board/` - ESPRESSIF MIT License
- `src/esp_codec_dev/` - Apache-2.0, Copyright Espressif Systems
- 自定义代码 (`music_mvp.ino`, `imu_monitor.html`, `user_config.h`) - 自定

---

## 注意事项

1. TCA9554 地址为 `0x20`, bit5=LCD复位(释放=1), bit7=PA功放使能
2. 当前固件**未启用 LVGL 显示**, QSPI 屏幕未初始化, 以节省内存和 CPU
3. ES8311 编解码器需要 MCLK 主时钟 (GPIO7)
4. IMU 数据通过 Web Serial API 读取，仅支持 Chrome/Edge 等 Chromium 内核浏览器
5. I2C 总线速度: ES8311/ES7210/TCA9554 = 100kHz, QMI8658 = 400kHz
6. 音频输出 16kHz 采样率, 如需更高音质可调整 `esp_codec_dev_sample_info_t`
