# music_mvp

> ESP32-S3 Motion-Controlled Music Interaction Prototype - IMU Gesture Triggers + Software Drum Synthesis + I2S Audio Output

Trigger different instrument sounds by shaking or tapping the dev board. Synthesized audio is output to a speaker via the ES8311 codec over I2S. A companion Web Dashboard provides real-time IMU visualization and audio debugging.

---

## Features

| Feature | Description |
|---------|-------------|
| **IMU Real-Time Data Stream** | QMI8658 6-axis sensor, 50Hz CSV output over serial |
| **Motion-Controlled Instruments** | 4 playback modes: Drumstick / Triangle / Maraca / Off, with velocity sensitivity |
| **Software Drum Synthesis** | Kick / Snare / Hat / Tom four-channel, sine + noise synthesis |
| **I2S Audio Output** | ES8311 DAC codec, 16kHz stereo, TCA9554 PA control |
| **Web Dashboard** | Chart.js real-time waveforms, 3D attitude cube, drum pads, mode switching |
| **Web Serial Communication** | Browser USB serial direct connection, no app installation required |
| **ESP-IDF Components** | Integrated codec_board + esp_codec_dev official audio framework |

---

## Hardware Specifications

| Component | Details |
|-----------|---------|
| **MCU** | ESP32-S3 |
| **Display** | Waveshare Touch-LCD-3.49 V2, 172x640, AXS15231B (display currently disabled in firmware) |
| **Audio DAC** | ES8311 @ I2C 0x18, I2S output (MCLK=7, BCLK=15, WS=46, DOUT=45) |
| **Audio ADC** | ES7210 @ I2C 0x40, I2S input (DIN=6), reserved for microphone |
| **PA Amplifier** | TCA9554 @ I2C 0x20, bit7 control |
| **IMU** | QMI8658 @ I2C 0x6B, 6-axis (accelerometer ±8G + gyroscope ±512dps) |
| **I2C Bus** | SDA=GPIO47, SCL=GPIO48 (shared by ES8311/ES7210/QMI8658/TCA9554) |
| **QSPI Display** | CS=GPIO9, PCLK=GPIO10, DATA0-3=GPIO11-14 (reserved, not initialized) |

---

## Project Structure

```
music_mvp/
├── music_mvp.ino              # Main firmware (Arduino/PlatformIO)
├── user_config.h              # Pin, I2C address, and display parameter configuration
├── imu_monitor.html           # Web Dashboard
├── README.md
│
├── src/
│   ├── codec_board/           # ESP-IDF audio board support package
│   │   ├── codec_board.h/c    # Board abstraction (I2C/I2S/Codec/LCD config)
│   │   ├── codec_init.h/c     # Codec initialization
│   │   ├── board_cfg.h/txt    # Board pin configuration descriptor
│   │   ├── cfg_parse.c        # Configuration parser
│   │   ├── drv/tca9554.c/h    # TCA9554 GPIO expander driver
│   │   ├── lcd_init.c         # LCD initialization (reserved)
│   │   └── dummy_codec.c/h    # Dummy codec (pure I2S)
│   │
│   └── esp_codec_dev/         # ESP-IDF audio codec device framework
│       ├── esp_codec_dev.c    # Device management core
│       ├── esp_codec_dev_vol.c # Software volume control
│       ├── device/es8311/     # ES8311 DAC driver
│       ├── device/es7210/     # ES7210 ADC driver
│       ├── platform/          # I2C/SPI/I2S/GPIO platform abstraction
│       └── interface/         # Codec interface definitions
│
└── build/                     # Build artifacts (excluded by .gitignore)
```

---

## Serial Communication Protocol

The Web Dashboard communicates with the ESP32 over serial at **115200bps**.

### Upstream (PC -> ESP32)

| Command | Description |
|---------|-------------|
| `K` / `S` / `H` / `T` | Trigger Kick / Snare / Hat / Tom drum sounds |
| `M0` ~ `M3` | Switch instrument mode (0=Drumstick, 1=Triangle, 2=Maraca, 3=Off) |
| `V0` ~ `V100` | Set volume percentage |

### Downstream (ESP32 -> PC)

```
ax,ay,az,gx,gy,gz
```
CSV format with 6 floating-point values, output at 50Hz:
- `ax,ay,az` - Accelerometer X/Y/Z (unit: m/s², ±8G range)
- `gx,gy,gz` - Gyroscope X/Y/Z (unit: deg/s, ±512dps range)

---

## Instrument Mode Descriptions

The firmware detects edge changes in IMU acceleration magnitude to trigger sounds:

| Mode | Command | Trigger Threshold | Cooldown | Sound |
|------|---------|-------------------|----------|-------|
| Drumstick | `M0` | >15 m/s² | 250ms | Velocity-controlled random drums (K/S/H/T) |
| Triangle | `M1` | >12 m/s² | 150ms | 2000Hz Ding decay tone |
| Maraca | `M2` | >12 m/s² | 150ms | White noise Shake decay tone |
| Off | `M3` | - | - | IMU data output only |

Velocity sensitivity: peak acceleration maps to a volume factor of 0.3~1.0, controlling synthesis amplitude.

---

## Quick Start

### 1. Environment Setup

- **Arduino IDE** or **PlatformIO**, with ESP32-S3 board support installed (esp32 >= 3.0)
- Install ESP-IDF audio components (codec_board, esp_codec_dev already included in `src/`)

### 2. Build & Flash

```bash
# PlatformIO
pio run --target upload

# Or open music_mvp.ino in Arduino IDE and compile/upload
```

Serial monitor (115200bps) will display initialization logs:
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

### 3. Open Web Dashboard

1. Open `imu_monitor.html` in Chrome/Edge browser
2. Click the **Connect** button and select the ESP32-S3 serial device
3. Once connected, view real-time IMU waveforms, switch instrument modes, and trigger drum sounds

---

## Audio Synthesis Algorithms

All software synthesis runs in real-time on the ESP32-S3. No external audio files required:

| Sound | Synthesis Method | Parameters |
|-------|-----------------|------------|
| **Kick** | 80Hz sine + 1kHz short attack pulse, exponential decay (exp(-12t)) | 4096 samples |
| **Snare** | 180Hz sine + white noise blend (70/30), exponential decay (exp(-8t)) | 4096 samples |
| **Hat** | Pure white noise, fast exponential decay (exp(-25t)) | 4096 samples |
| **Tom** | 200Hz sine + frequency sweep down (0.7x), exponential decay (exp(-5t)) | 4096 samples |
| **Triangle** | 2000Hz sine, decay (exp(-80t)) | 800 samples (~50ms) |
| **Maraca** | White noise, decay (exp(-60t)) | 640 samples (~40ms) |

Output format: 16kHz, 16bit, stereo interleaved (I2S standard format).

---

## Architecture Design

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

- **IMU Task** runs on FreeRTOS Core 1, sampling at 50Hz and outputting CSV
- **loop()** runs on Core 0, handling serial command parsing
- **Audio Synthesis** writes directly to the I2S buffer within the IMU task, no extra task needed
- **Gesture Detection** uses an edge-detection state machine: Idle -> Swing Peak Tracking -> Trigger -> Cooldown

---

## License

- `src/codec_board/` - ESPRESSIF MIT License
- `src/esp_codec_dev/` - Apache-2.0, Copyright Espressif Systems
- Custom code (`music_mvp.ino`, `imu_monitor.html`, `user_config.h`) - Custom

---

## Notes

1. TCA9554 address is `0x20`, bit5=LCD reset (release=1), bit7=PA amplifier enable
2. Current firmware does **not enable LVGL display**; QSPI screen is uninitialized to save memory and CPU
3. ES8311 codec requires MCLK master clock (GPIO7)
4. IMU data is read via the Web Serial API, supported only by Chromium-based browsers (Chrome/Edge)
5. I2C bus speeds: ES8311/ES7210/TCA9554 = 100kHz, QMI8658 = 400kHz
6. Audio output at 16kHz sample rate; adjust `esp_codec_dev_sample_info_t` if higher quality is needed
