/*
 * music_mvp v1.0
 * ESP32-S3 Waveshare Touch-LCD-3.49 V2
 *
 * 功能:
 *  - QMI8658 IMU 数据流 @50Hz → Serial CSV 输出
 *  - ES8311 I2S 音频 → 鼓垫播放 (Serial 指令触发)
 *  - 屏幕关闭 (QSPI 未初始化)
 *
 * 调试: /Users/luo/Desktop/music_mvp/imu_monitor.html
 *
 * 串口协议:
 *   发送 (ESP32→PC): "ax,ay,az,gx,gy,gz\n" CSV @50Hz
 *   接收 (PC→ESP32): 'K'=Kick  'S'=Snare  'H'=Hat  'T'=Tom
 *
 * 引脚:
 *   I2C: SDA=47, SCL=48   (ES8311, ES7210, QMI8658, TCA9554)
 *   I2S: MCLK=7, BCLK=15, WS=46, DOUT=45, DIN=6
 *   PA:  TCA9554 @ 0x20 bit7
 */

#include "user_config.h"
#include "src/codec_board/codec_board.h"
#include "src/codec_board/codec_init.h"
#include <driver/i2c_master.h>
#include <math.h>

extern esp_codec_dev_handle_t get_record_handle(void);

// ── I2C 预初始化 ──────────────────────────────
static i2c_master_bus_handle_t g_i2c_bus = NULL;
static void i2c_preinit() {
  i2c_master_bus_config_t cfg = {};
  cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  cfg.i2c_port = I2C_NUM_0;
  cfg.sda_io_num = GPIO_NUM_47;
  cfg.scl_io_num = GPIO_NUM_48;
  cfg.glitch_ignore_cnt = 7;
  cfg.flags.enable_internal_pullup = true;
  esp_err_t ret = i2c_new_master_bus(&cfg, &g_i2c_bus);
  Serial.printf("[INIT] i2c_new_master_bus: ret=%d bus=%p\n", ret, g_i2c_bus);
}

// ── PA 控制 (TCA9554 bit7) ────────────────────
static i2c_master_dev_handle_t tca9554_dev = NULL;

static bool tca9554_write(uint8_t reg, uint8_t val) {
  if (!tca9554_dev) return false;
  uint8_t buf[] = {reg, val};
  return i2c_master_transmit(tca9554_dev, buf, 2, 100) == ESP_OK;
}

static void pa_init() {
  i2c_master_bus_handle_t bus = (i2c_master_bus_handle_t)get_i2c_bus_handle(0);
  if (!bus) { Serial.println("[PA] FAIL: no I2C bus"); return; }

  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = 0x20;
  dev_cfg.scl_speed_hz = 100000;
  if (i2c_master_bus_add_device(bus, &dev_cfg, &tca9554_dev) != ESP_OK) {
    Serial.println("[PA] FAIL: add_device");
    return;
  }
  tca9554_write(0x03, 0x7F);   // Config: bit7=output
  delay(2);
  tca9554_write(0x01, 0xA0);   // Output: bit7=1(PA on), bit5=1(LCD RST released)
  delay(10);
  Serial.println("[PA] Speaker amp ON (TCA9554 bit7=1)");
}

// ── QMI8658 IMU ──────────────────────────────
#define QMI8658_ADDR       0x6B
#define QMI8658_REG_WHOAMI 0x00
#define QMI8658_REG_CTRL1  0x02
#define QMI8658_REG_CTRL2  0x03
#define QMI8658_REG_CTRL3  0x04
#define QMI8658_REG_CTRL5  0x06
#define QMI8658_REG_CTRL7  0x08
#define QMI8658_REG_CTRL8  0x09
#define QMI8658_REG_RESET  0x60
#define QMI8658_REG_RST    0x4D
#define QMI8658_REG_AX_L   0x35
#define QMI8658_REG_GX_L   0x3B

static i2c_master_dev_handle_t qmi8658_dev = NULL;

static bool qmi8658_write(uint8_t reg, uint8_t val) {
  if (!qmi8658_dev) return false;
  uint8_t buf[] = {reg, val};
  return i2c_master_transmit(qmi8658_dev, buf, 2, 100) == ESP_OK;
}

static bool qmi8658_read(uint8_t reg, uint8_t *buf, uint8_t len) {
  if (!qmi8658_dev) return false;
  return i2c_master_transmit_receive(qmi8658_dev, &reg, 1, buf, len, 100) == ESP_OK;
}

static bool qmi8658_begin() {
  i2c_master_bus_handle_t bus = (i2c_master_bus_handle_t)get_i2c_bus_handle(0);
  if (!bus) { Serial.println("[IMU] FAIL: no I2C bus"); return false; }

  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = QMI8658_ADDR;
  dev_cfg.scl_speed_hz = 400000;
  if (i2c_master_bus_add_device(bus, &dev_cfg, &qmi8658_dev) != ESP_OK) {
    Serial.println("[IMU] FAIL: add_device (QMI8658 not found?)");
    return false;
  }

  // Step 1: Reset
  qmi8658_write(QMI8658_REG_RESET, 0xB0);
  delay(15);

  // Step 2: Check reset result
  uint8_t rst = 0;
  qmi8658_read(QMI8658_REG_RST, &rst, 1);
  Serial.printf("[IMU] RST_RESULT=0x%02X (expect 0x80)\n", rst);

  // Step 3: Enable address auto-increment (CTRL1 bit6)
  qmi8658_write(QMI8658_REG_CTRL1, 0x60);

  // Step 4: Check WHO_AM_I
  uint8_t who = 0;
  qmi8658_read(QMI8658_REG_WHOAMI, &who, 1);
  Serial.printf("[IMU] WHO_AM_I=0x%02X (expect 0x05)\n", who);
  if (who != 0x05) {
    Serial.println("[IMU] FAIL: wrong WHO_AM_I");
    return false;
  }

  // Step 5: Write CTRL8=0x80
  qmi8658_write(QMI8658_REG_CTRL8, 0x80);

  // Step 6: Configure sensors
  // CTRL2 bits[7:4]=accel range (2=±8G), bits[3:0]=ODR (3=1000Hz)
  qmi8658_write(QMI8658_REG_CTRL2, (2 << 4) | 3);  // 0x23
  // CTRL3 bits[7:4]=gyro range (5=512dps)
  qmi8658_write(QMI8658_REG_CTRL3, 5 << 4);         // 0x50
  // CTRL5: accel LPF
  qmi8658_write(QMI8658_REG_CTRL5, 0x00);
  // CTRL7: enable accel(bit0) + gyro(bit1)
  qmi8658_write(QMI8658_REG_CTRL7, 0x03);

  Serial.println("[IMU] QMI8658 initialized OK");
  return true;
}

static bool imu_read(float *ax, float *ay, float *az, float *gx, float *gy, float *gz) {
  uint8_t buf[12];
  if (!qmi8658_read(QMI8658_REG_AX_L, buf, 12)) return false;

  int16_t raw_ax = (int16_t)(buf[0]  | (buf[1]  << 8));
  int16_t raw_ay = (int16_t)(buf[2]  | (buf[3]  << 8));
  int16_t raw_az = (int16_t)(buf[4]  | (buf[5]  << 8));
  int16_t raw_gx = (int16_t)(buf[6]  | (buf[7]  << 8));
  int16_t raw_gy = (int16_t)(buf[8]  | (buf[9]  << 8));
  int16_t raw_gz = (int16_t)(buf[10] | (buf[11] << 8));

  // Accel: ±8G, scale = 8*9.80665/32768
  *ax = raw_ax * (8.0f * 9.80665f / 32768.0f);
  *ay = raw_ay * (8.0f * 9.80665f / 32768.0f);
  *az = raw_az * (8.0f * 9.80665f / 32768.0f);

  // Gyro: ±512dps, scale = 512/32768
  *gx = raw_gx * (512.0f / 32768.0f);
  *gy = raw_gy * (512.0f / 32768.0f);
  *gz = raw_gz * (512.0f / 32768.0f);

  return true;
}

// ── 全局状态 ──────────────────────────────────
static int vol_percent = 80;
static int instrument_mode = 4;  // 0=鼓棒 1=三角铁 2=沙锤 3=关闭 4=自动(竖挥鼓棒/横挥沙锤)

// ── IMU 数据流任务 ────────────────────────────
static void imu_task(void *arg) {
  float ax, ay, az, gx, gy, gz;
  TickType_t last = xTaskGetTickCount();
  
  // 手势检测共用状态
  float peak_mag = 0;
  uint32_t cooldown_end = 0;
  uint32_t peak_time = 0;
  bool swinging = false;
  bool must_rest = false;
  
  // 重力方向估算 (自动模式用)
  float gx_est = 0, gy_est = 0, gz_est = 9.8f;
  bool g_calibrated = false;
  
  // 挥动方向追踪 (自动模式用)
  float vert_sum = 0, horiz_sum = 0;
  int swing_samples = 0;
  
  for (;;) {
    if (imu_read(&ax, &ay, &az, &gx, &gy, &gz)) {
      Serial.printf("%.3f,%.3f,%.3f,%.1f,%.1f,%.1f\n", ax, ay, az, gx, gy, gz);
      
      uint32_t now = millis();
      float mag = sqrtf(ax*ax + ay*ay + az*az);
      
      // ── 重力方向估算: 静止时用 EMA 学习重力向量 ──
      if (fabsf(mag - 9.8f) < 1.5f) {
        float alpha = 0.008f;
        gx_est = gx_est * (1.0f - alpha) + ax * alpha;
        gy_est = gy_est * (1.0f - alpha) + ay * alpha;
        gz_est = gz_est * (1.0f - alpha) + az * alpha;
        if (!g_calibrated && fabsf(gz_est) > 1.0f) g_calibrated = true;
      }
      
      if (instrument_mode == 0) {
        // ── 鼓棒: 总幅值边沿检测 (固定 Snare) ──
        // 静止检测
        if (must_rest && now > cooldown_end && mag > 10.0f && mag < 11.0f) {
          must_rest = false;
        }
        if (!must_rest && mag > 15.0f && now > cooldown_end) {
          swinging = true; peak_mag = mag;
        }
        if (swinging) {
          if (mag > peak_mag) { peak_mag = mag; peak_time = now; }
          if (mag < 13.0f || (now - peak_time) > 500) {
            float vf = (peak_mag - 15.0f) / 42.0f;
            if (vf < 0.3f) vf = 0.3f; if (vf > 1.0f) vf = 1.0f;
            play_drum_with_force(vf);
            cooldown_end = now + 400; swinging = false; must_rest = true;
          }
        }
      } else if (instrument_mode == 1 || instrument_mode == 2) {
        // ── 三角铁/沙锤 ──
        if (!swinging) {
          if (mag > 12.0f && now > cooldown_end) {
            swinging = true; peak_mag = mag;
          }
        } else {
          if (mag > peak_mag) peak_mag = mag;
          if (mag < 11.0f) {
            float vf = (peak_mag - 12.0f) / 18.0f;
            if (vf < 0.3f) vf = 0.3f; if (vf > 1.0f) vf = 1.0f;
            if (instrument_mode == 1) play_triangle_ding(vf);
            else play_maraca_shake(vf);
            cooldown_end = now + 150; swinging = false;
          }
        }
      } else if (instrument_mode == 4 && g_calibrated) {
        // ── 自动模式: 根据挥动方向自动选择乐器 ──
        // 竖着挥 → 鼓棒(Snare), 横着挥 → 沙锤
        if (must_rest && now > cooldown_end && mag > 10.0f && mag < 11.0f) {
          must_rest = false;
        }
        
        if (!swinging) {
          if (!must_rest && mag > 15.0f && now > cooldown_end) {
            swinging = true; peak_mag = mag;
            vert_sum = 0; horiz_sum = 0; swing_samples = 0;
          }
        }
        
        if (swinging) {
          if (mag > peak_mag) { peak_mag = mag; peak_time = now; }
          
          // 累积运动方向: 投影到重力方向 vs 垂直重力方向
          float mx = ax - gx_est, my = ay - gy_est, mz = az - gz_est;
          float gmag = sqrtf(gx_est*gx_est + gy_est*gy_est + gz_est*gz_est);
          if (gmag > 0.1f) {
            float gux = gx_est / gmag, guy = gy_est / gmag, guz = gz_est / gmag;
            float vert_proj = mx*gux + my*guy + mz*guz;  // 带符号投影
            float vert = fabsf(vert_proj);
            float hx = mx - vert_proj * gux, hy = my - vert_proj * guy, hz = mz - vert_proj * guz;
            float horiz = sqrtf(hx*hx + hy*hy + hz*hz);
            vert_sum += vert; horiz_sum += horiz; swing_samples++;
          }
          
          // 释放触发
          if (mag < 13.0f || (now - peak_time) > 500) {
            float vf = (peak_mag - 15.0f) / 42.0f;
            if (vf < 0.3f) vf = 0.3f; if (vf > 1.0f) vf = 1.0f;
            
            if (swing_samples > 2) {
              float avg_h = horiz_sum / swing_samples;
              float avg_v = vert_sum / swing_samples;
              // 竖直分量明显大于水平 → 鼓棒; 否则 → 沙锤
              if (avg_v > avg_h * 1.2f) {
                play_drum_with_force(vf);
              } else {
                play_maraca_shake(vf);
              }
            } else {
              play_drum_with_force(vf);  // 默认鼓棒
            }
            cooldown_end = now + 400; swinging = false; must_rest = true;
          }
        }
      }
    }
    vTaskDelayUntil(&last, pdMS_TO_TICKS(20));  // 50Hz
  }
}

// ── ES8311 音频初始化 ─────────────────────────
static esp_codec_dev_handle_t playback = NULL;
static esp_codec_dev_handle_t capture = NULL;

static void audio_init() {
  set_codec_board_type("S3_LCD_3_49");

  codec_init_cfg_t codec_cfg = {};
  codec_cfg.in_mode = CODEC_I2S_MODE_TDM;
  codec_cfg.out_mode = CODEC_I2S_MODE_TDM;
  codec_cfg.in_use_tdm = false;
  codec_cfg.reuse_dev = false;

  esp_err_t ret = init_codec(&codec_cfg);
  if (ret != ESP_OK) {
    Serial.printf("[AUDIO] init_codec FAILED: 0x%x\n", ret);
    return;
  }

  delay(100);  // 等 ES7210 内部时钟稳定

  playback = get_playback_handle();
  capture = get_record_handle();
  Serial.printf("[AUDIO] Playback handle: %s, Capture handle: %s\n",
    playback ? "OK" : "NULL", capture ? "OK" : "NULL");

  esp_codec_dev_set_out_vol(playback, 100.0);
  if (capture) esp_codec_dev_set_in_gain(capture, 35.0);
  
  esp_codec_dev_sample_info_t fs = {};
  fs.sample_rate = 24000;
  fs.channel = 2;
  fs.bits_per_sample = 16;
  Serial.println("[AUDIO] Opening playback...");
  esp_err_t r = esp_codec_dev_open(playback, &fs);
  if (r != ESP_OK) Serial.printf("[AUDIO] Playback open FAIL: 0x%x\n", r);
  else Serial.println("[AUDIO] Playback open OK");
  
  if (capture) {
    Serial.println("[AUDIO] Opening capture...");
    r = esp_codec_dev_open(capture, &fs);
    Serial.printf("[AUDIO] Capture open: %s (0x%x)\n", r == ESP_OK ? "OK" : "FAIL", r);
  }
}

// ── 鼓声合成 (vol_factor: 0.3~1.0 力度) ──────────
#define DRUM_LEN 4096
static int16_t drum_buf[DRUM_LEN * 2];  // stereo interleaved
static const float DRUM_SR = 24000.0f;

static void drum_kick(float vol) {
  float amp = vol * DRUM_SR;
  for (int i = 0; i < DRUM_LEN; i++) {
    float t = (float)i / DRUM_SR;
    float env = expf(-12.0f * t);
    float s = env * sinf(2.0f * (float)M_PI * 80.0f * t);
    if (i < 80) s += (1.0f - i / 80.0f) * 0.5f * sinf(2.0f * (float)M_PI * 1000.0f * t);
    int16_t v = (int16_t)(s * amp);
    drum_buf[i * 2] = v;
    drum_buf[i * 2 + 1] = v;
  }
  if (playback) esp_codec_dev_write(playback, drum_buf, DRUM_LEN * 4);
}

static void drum_snare(float vol) {
  float amp = vol * 14000.0f;
  uint32_t seed = 42;
  for (int i = 0; i < DRUM_LEN; i++) {
    float t = (float)i / DRUM_SR;
    float env = expf(-8.0f * t);
    seed = seed * 1103515245 + 12345;
    float noise = (float)(int16_t)(seed >> 16) / 32768.0f;
    float tone = sinf(2.0f * (float)M_PI * 180.0f * t);
    float s = env * (noise * 0.7f + tone * 0.3f);
    int16_t v = (int16_t)(s * amp);
    drum_buf[i * 2] = v;
    drum_buf[i * 2 + 1] = v;
  }
  if (playback) esp_codec_dev_write(playback, drum_buf, DRUM_LEN * 4);
}

static void drum_hat(float vol) {
  float amp = vol * 10000.0f;
  uint32_t seed = 99;
  for (int i = 0; i < DRUM_LEN; i++) {
    float t = (float)i / DRUM_SR;
    float env = expf(-25.0f * t);
    seed = seed * 1103515245 + 12345;
    float noise = (float)(int16_t)(seed >> 16) / 32768.0f;
    float s = env * noise * 0.6f;
    int16_t v = (int16_t)(s * amp);
    drum_buf[i * 2] = v;
    drum_buf[i * 2 + 1] = v;
  }
  if (playback) esp_codec_dev_write(playback, drum_buf, DRUM_LEN * 4);
}

static void drum_tom(float vol) {
  float amp = vol * 13000.0f;
  for (int i = 0; i < DRUM_LEN; i++) {
    float t = (float)i / DRUM_SR;
    float env = expf(-5.0f * t);
    float f = 200.0f * (1.0f - 0.3f * (1.0f - env));  // frequency glide
    float s = env * sinf(2.0f * (float)M_PI * f * t);
    int16_t v = (int16_t)(s * amp);
    drum_buf[i * 2] = v;
    drum_buf[i * 2 + 1] = v;
  }
  if (playback) esp_codec_dev_write(playback, drum_buf, DRUM_LEN * 4);
}

// 手动按键触发 (全力度)
static void play_drum(char cmd) {
  if (!playback) return;
  switch (cmd) {
    case 'K': drum_kick(1.0f);  break;
    case 'S': drum_snare(1.0f); break;
    case 'H': drum_hat(1.0f);   break;
    case 'T': drum_tom(1.0f);   break;
  }
}

// ── 力度鼓声 (手势触发, 根据力度随机鼓 + 音量) ──
static void play_drum_with_force(float vol_factor) {
  if (!playback) return;
  // 固定用 Snare
  drum_snare(vol_factor);
}

// ── 乐器合成 ──────────────────────────────────
static void play_triangle_ding(float vol_factor) {
  // 2000Hz正弦波, 指数衰减包络 exp(-80*t), ~50ms @16kHz 立体声
  int len = 800;
  // 力度映射到 0.3~1.0 的振幅因子
  float amp = 0.3f + vol_factor * 0.7f;
  for (int i = 0; i < len; i++) {
    float t = (float)i / DRUM_SR;
    float env = expf(-80.0f * t);
    float s = amp * env * sinf(2.0f * M_PI * 2000.0f * t);
    int16_t v = (int16_t)(s * 12000.0f);
    drum_buf[i*2] = v; drum_buf[i*2+1] = v;
  }
  if (playback) esp_codec_dev_write(playback, drum_buf, len * 4);
}

static void play_maraca_shake(float vol_factor) {
  uint32_t seed = 777;
  int len = 640;
  float amp = 0.3f + vol_factor * 0.7f;
  for (int i = 0; i < len; i++) {
    float t = (float)i / DRUM_SR;
    float env = expf(-60.0f * t);
    seed = seed * 1103515245 + 12345;
    float noise = (float)(int16_t)(seed >> 16) / 32768.0f;
    float s = amp * env * noise * 0.8f;
    int16_t v = (int16_t)(s * 10000.0f);
    drum_buf[i*2] = v; drum_buf[i*2+1] = v;
  }
  if (playback) esp_codec_dev_write(playback, drum_buf, len * 4);
}

// ── 麦克风音量任务 ──────────────────────────
static void mic_task(void *arg) {
  Serial.println("[MIC] Task started");
  int16_t samples[512 * 2];
  uint32_t last_print = 0;
  bool diag_done = false;
  for (;;) {
    uint32_t now = millis();
    if (capture) {
      // 首次诊断：读取 ES7210 寄存器确认芯片状态
      if (!diag_done) {
        diag_done = true;
        int rv;
        esp_codec_dev_read_reg(capture, 0x01, &rv);  // CLOCK_OFF_REG01
        Serial.printf("[MIC_DIAG] CLOCK_OFF=0x%02X ", rv);
        esp_codec_dev_read_reg(capture, 0x00, &rv);  // RESET_REG00
        Serial.printf("RESET=0x%02X ", rv);
        esp_codec_dev_read_reg(capture, 0x06, &rv);  // POWER_DOWN
        Serial.printf("POWER=0x%02X\n", rv);
      }
      
      int rd = esp_codec_dev_read(capture, samples, sizeof(samples));
      if (rd > 0) {
        int count = rd / 2;
        double sum = 0.0;
        for (int i = 0; i < count; i++) {
          double s = (double)samples[i];
          sum += s * s;
        }
        double rms = sqrt(sum / count);
        double vol = (rms / 32768.0) * 100.0;
        Serial.printf("!%d\n", (int)vol);
      } else {
        if (now - last_print > 1000) {
          Serial.printf("!0\n");
          last_print = now;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ── 主程序 ────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("\n╔═══════════════════════════════════════╗");
  Serial.println(  "║  music_mvp v1.0                      ║");
  Serial.println(  "║  IMU @50Hz + Drum Pad               ║");
  Serial.println(  "║  Commands: K/S/H/T = drum sounds    ║");
  Serial.println(  "╚═══════════════════════════════════════╝\n");

  // 1. I2C bus
  i2c_preinit();

  // 2. PA (must be before audio init on this code path)
  pa_init();

  // 3. Audio codec (I2S + ES8311)
  audio_init();

  // 3.5 Microphone capture
  xTaskCreatePinnedToCore(mic_task, "mic", 4096, NULL, 1, NULL, 1);

  // 4. IMU sensor
  if (!qmi8658_begin()) {
    Serial.println("[MAIN] IMU init failed - continuing without IMU");
  } else {
    xTaskCreatePinnedToCore(imu_task, "IMU", 4096, NULL, 3, NULL, 1);
    Serial.println("[MAIN] IMU streaming started (core 1, 50Hz)");
  }

  Serial.println("[MAIN] === READY ===\n");
}

static char cmd_buf[8];
static int cmd_idx = 0;

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    
    // Single-char drum commands
    if (c == 'K' || c == 'S' || c == 'H' || c == 'T') {
      play_drum(c);
      cmd_idx = 0;  // reset buffer
      continue;
    }
    
    // 乐器模式切换: M + 数字(0-3)
    if (c == 'M') {
      unsigned long timeout = millis() + 50;
      while (!Serial.available() && millis() < timeout) delay(1);
      if (Serial.available()) {
        char d = Serial.read();
        if (d >= '0' && d <= '4') {
          instrument_mode = d - '0';
          Serial.printf("[MODE] Set to %d\n", instrument_mode);
        }
      }
      cmd_idx = 0;
      continue;
    }
    
    // Multi-char volume command: V + digits
    if (c == 'V' || cmd_idx > 0) {
      if (cmd_idx == 0 && c == 'V') {
        cmd_buf[cmd_idx++] = c;
      } else if (cmd_idx > 0 && c >= '0' && c <= '9') {
        cmd_buf[cmd_idx++] = c;
        if (cmd_idx >= 7) cmd_idx = 0;  // safety
      } else {
        // End of volume command: parse and apply
        if (cmd_idx >= 2) {
          cmd_buf[cmd_idx] = 0;
          int v = atoi(&cmd_buf[1]);  // skip 'V'
          if (v >= 0 && v <= 100) {
            vol_percent = v;
            if (playback) {
              esp_codec_dev_set_out_vol(playback, (float)v);
              Serial.printf("[VOL] Set to %d%%\n", v);
            }
          }
        }
        cmd_idx = 0;
        if (c == 'V') cmd_buf[cmd_idx++] = 'V';  // start new V command
      }
      continue;
    }
    
    cmd_idx = 0;  // unknown char, reset
  }
  delay(5);
}
