#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl_port.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "user_config.h"
#include "driver/spi_master.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "src/axs15231b/esp_lcd_axs15231b.h"
#include "i2c_bsp.h"
#include "exio_bsp.h"

// Arduino LVGL package does not compile demos/ by default.
// Use a lightweight diagnostic UI that still exercises the official
// AXS15231B panel path (QSPI mode3 + full_refresh).

#define LCD_BIT_PER_PIXEL (16)

static const char *TAG = "lvgl_port";
static SemaphoreHandle_t lvgl_mux = NULL;

static uint16_t *lvgl_dma_buf = NULL; 
static SemaphoreHandle_t lvgl_flush_semap;

#if (Rotated == USER_DISP_ROT_90)
uint16_t* rotat_ptr = NULL;
#endif

static const axs15231b_lcd_init_cmd_t lcd_init_cmds[] = 
{
  {0x11, (uint8_t []){0x00}, 0, 100},
  {0x29, (uint8_t []){0x00}, 0, 100},
};

static bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
  BaseType_t TaskWoken;
  xSemaphoreGiveFromISR(lvgl_flush_semap,&TaskWoken);
  return false;
}

static void example_increase_lvgl_tick(void *arg)
{
  lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

static void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
#if (Rotated == USER_DISP_ROT_90)
  uint32_t index = 0;
  uint16_t *data_ptr = (uint16_t *)color_map;
  for (uint16_t j = 0; j < EXAMPLE_LCD_H_RES; j++)
  {
    for (uint16_t i = 0; i < EXAMPLE_LCD_V_RES; i++)
    {
      rotat_ptr[index++] = data_ptr[EXAMPLE_LCD_H_RES * (EXAMPLE_LCD_V_RES - i - 1) + j];             
    }
  }
#endif
  esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;
  const int flush_coun = (LVGL_SPIRAM_BUFF_LEN / LVGL_DMA_BUFF_LEN);
  const int offgap = (LCD_NOROT_VRES / flush_coun);
  const int dmalen = (LVGL_DMA_BUFF_LEN / 2);
  int offsetx1 = 0;
  int offsety1 = 0;
  int offsetx2 = LCD_NOROT_HRES;
  int offsety2 = offgap;

#if (Rotated == USER_DISP_ROT_90)
  uint16_t *map = (uint16_t *)rotat_ptr;
#else
  uint16_t *map = (uint16_t *)color_map;
#endif

  xSemaphoreGive(lvgl_flush_semap);
  for(int i = 0; i<flush_coun; i++)
  {
    xSemaphoreTake(lvgl_flush_semap,portMAX_DELAY);
    memcpy(lvgl_dma_buf,map,LVGL_DMA_BUFF_LEN);
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2, offsety2, lvgl_dma_buf);
    offsety1 += offgap;
    offsety2 += offgap;
    map += dmalen;
  }
  xSemaphoreTake(lvgl_flush_semap,portMAX_DELAY);
  lv_disp_flush_ready(drv);
}

static void example_lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
  //static uint8_t read_touchpad_cmd[8] = {0xb5, 0xab, 0xa5, 0x5a, 0x0, 0x0, 0x0, 0x8};
  uint8_t read_touchpad_cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x0, 0x0, 0x0, 0x0e,0x0, 0x0, 0x0};
  uint8_t buff[32] = {0};
  memset(buff,0,32);
  ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_write_read_dev(disp_touch_dev_handle,read_touchpad_cmd,11,buff,32));
  uint16_t pointX;
  uint16_t pointY;
  pointX = (((uint16_t)buff[2] & 0x0f) << 8) | (uint16_t)buff[3];
  pointY = (((uint16_t)buff[4] & 0x0f) << 8) | (uint16_t)buff[5];
  //ESP_LOGI("Touch","%d,%d",buff[0],buff[1]);
  if (buff[1]>0 && buff[1]<5)
  {
    data->state = LV_INDEV_STATE_PR;
#if (Rotated == USER_DISP_ROT_NONO)
    if(pointX > EXAMPLE_LCD_V_RES) pointX = EXAMPLE_LCD_V_RES;
    if(pointY > EXAMPLE_LCD_H_RES) pointY = EXAMPLE_LCD_H_RES;
    data->point.x = pointY;
    data->point.y = (EXAMPLE_LCD_V_RES-pointX);
#else
    if(pointX > EXAMPLE_LCD_H_RES) pointX = EXAMPLE_LCD_H_RES;
    if(pointY > EXAMPLE_LCD_V_RES) pointY = EXAMPLE_LCD_V_RES;
    data->point.x = (EXAMPLE_LCD_H_RES - pointX);
    data->point.y = (EXAMPLE_LCD_V_RES - pointY);
#endif
  }
  else 
  {
    data->state = LV_INDEV_STATE_REL;
  }
}

static bool example_lvgl_lock(int timeout_ms)
{
  const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
  return xSemaphoreTake(lvgl_mux, timeout_ticks) == pdTRUE;       
}

static void example_lvgl_unlock(void)
{
  assert(lvgl_mux && "bsp_display_start must be called first");
  xSemaphoreGive(lvgl_mux);
}

void example_lvgl_port_task(void *arg)
{
  uint32_t task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
  for(;;)
  {
    if (example_lvgl_lock(-1)) 
    {
      task_delay_ms = lv_timer_handler();
      //Release the mutex
      example_lvgl_unlock();
    }
    if (task_delay_ms > EXAMPLE_LVGL_TASK_MAX_DELAY_MS)
    {
      task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
    } else if (task_delay_ms < EXAMPLE_LVGL_TASK_MIN_DELAY_MS)
    {
      task_delay_ms = EXAMPLE_LVGL_TASK_MIN_DELAY_MS;
    }
    vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
  }
}

void lvgl_port_init(void)
{
#if (Rotated == USER_DISP_ROT_90)
  rotat_ptr = (uint16_t*)heap_caps_malloc(EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
  assert(rotat_ptr);
#endif
  lvgl_flush_semap = xSemaphoreCreateBinary();

  static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s) called draw buffer(s)
  static lv_disp_drv_t disp_drv;      // contains callback functions
  // V2: LCD RST/BL via TCA9554 EXIO (NOT GPIO21/8)
  ESP_LOGI(TAG, "Initialize V2 EXIO (LCD RST / BL)");
  if (!exio_bsp_init()) {
    ESP_LOGE(TAG, "EXIO init failed — panel reset may be stuck");
  }

  ESP_LOGI(TAG, "Initialize QSPI bus");
  spi_bus_config_t buscfg = {};
    buscfg.data0_io_num = EXAMPLE_PIN_NUM_LCD_DATA0;
    buscfg.data1_io_num = EXAMPLE_PIN_NUM_LCD_DATA1;
    buscfg.sclk_io_num = EXAMPLE_PIN_NUM_LCD_PCLK;
    buscfg.data2_io_num = EXAMPLE_PIN_NUM_LCD_DATA2;
    buscfg.data3_io_num = EXAMPLE_PIN_NUM_LCD_DATA3;
    buscfg.max_transfer_sz = LVGL_DMA_BUFF_LEN;
  ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

  ESP_LOGI(TAG, "Install panel IO");
	  esp_lcd_panel_io_handle_t panel_io = NULL;
    esp_lcd_panel_handle_t panel = NULL;
    
  esp_lcd_panel_io_spi_config_t io_config = {};
	io_config.cs_gpio_num = EXAMPLE_PIN_NUM_LCD_CS;                 
    io_config.dc_gpio_num = -1;          
    io_config.spi_mode = 3;              
    io_config.pclk_hz = 40 * 1000 * 1000;
    io_config.trans_queue_depth = 10;    
    io_config.on_color_trans_done = example_notify_lvgl_flush_ready; 
    //io_config.user_ctx = &disp_drv,         
    io_config.lcd_cmd_bits = 32;         
    io_config.lcd_param_bits = 8;        
    io_config.flags.quad_mode = true;                         
	ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_config, &panel_io));
    
  axs15231b_vendor_config_t vendor_config = {};
    vendor_config.flags.use_qspi_interface = 1;
    vendor_config.init_cmds = lcd_init_cmds;
    vendor_config.init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]);
    
  esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = -1;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = LCD_BIT_PER_PIXEL;
    panel_config.vendor_config = &vendor_config;

  ESP_LOGI(TAG, "Install panel driver");
  ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(panel_io, &panel_config, &panel));

  // V2 hardware reset via EXIO bit5 (active low)
  if (!exio_bsp_lcd_reset_pulse()) {
    ESP_LOGW(TAG, "EXIO reset pulse failed, continue with software init");
  }
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

  lv_init();

  lvgl_dma_buf = (uint16_t *)heap_caps_malloc(LVGL_DMA_BUFF_LEN , MALLOC_CAP_DMA);
  assert(lvgl_dma_buf);
  lv_color_t *buffer_1 = (lv_color_t *)heap_caps_malloc(LVGL_SPIRAM_BUFF_LEN , MALLOC_CAP_SPIRAM);
  lv_color_t *buffer_2 = (lv_color_t *)heap_caps_malloc(LVGL_SPIRAM_BUFF_LEN , MALLOC_CAP_SPIRAM);
  assert(buffer_1);
  assert(buffer_2);
  lv_disp_draw_buf_init(&disp_buf, buffer_1, buffer_2, EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES);

  ESP_LOGI(TAG, "Register display driver to LVGL");
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = EXAMPLE_LCD_H_RES;
  disp_drv.ver_res = EXAMPLE_LCD_V_RES;
  disp_drv.flush_cb = example_lvgl_flush_cb;
  disp_drv.draw_buf = &disp_buf;
  disp_drv.full_refresh = 1;          //full_refresh must be 1
  disp_drv.user_data = panel;
  lv_disp_drv_register(&disp_drv);

  ESP_LOGI(TAG, "Install LVGL tick timer");
  esp_timer_create_args_t lvgl_tick_timer_args = {};
    lvgl_tick_timer_args.callback = &example_increase_lvgl_tick;
    lvgl_tick_timer_args.name = "lvgl_tick";
  esp_timer_handle_t lvgl_tick_timer = NULL;
  ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer,EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

  static lv_indev_drv_t indev_drv;    // Input device driver (Touch)
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = example_lvgl_touch_cb;
  lv_indev_drv_register(&indev_drv);

  lvgl_mux = xSemaphoreCreateMutex();
  assert(lvgl_mux);
  xTaskCreatePinnedToCore(example_lvgl_port_task, "LVGL", 4000, NULL, 4, NULL,0); //运行于内核_0
  if (example_lvgl_lock(-1))
  {
    // Diagnostic UI on official panel stack (no lv_demo_widgets dependency)
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x003366), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, EXAMPLE_LCD_H_RES, 80);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x00C853), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 8, 0);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label,
      "Waveshare 3.49 V2\n"
      "Official AXS15231B path\n"
      "EXIO RST/BL\n"
      "172x640 OK?");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 40);

    lv_obj_t *r = lv_obj_create(scr);
    lv_obj_set_size(r, EXAMPLE_LCD_H_RES - 20, 60);
    lv_obj_align(r, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_bg_color(r, lv_color_hex(0xD50000), 0);
    lv_obj_set_style_border_width(r, 0, 0);

    example_lvgl_unlock();
    ESP_LOGI(TAG, "diagnostic UI started");
  }
}