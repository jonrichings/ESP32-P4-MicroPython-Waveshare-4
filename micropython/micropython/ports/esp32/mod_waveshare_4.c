/*
 * MicroPython Waveshare ESP32-P4 WIFI6-Touch-LCD-4B Hardware Module
 */

#include "py/mphal.h"
#include "py/obj.h"
#include "py/runtime.h"

// ESP-IDF Drivers
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"

#include "py/misc.h"

#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7703.h"

#include "esp_ldo_regulator.h"

#include "esp_lcd_touch_gt911.h"
#include "lvgl.h"

static const char *TAG = "mod_waveshare_4";

// Persistent Hardware Handles (Singletons)
static esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
static esp_lcd_panel_io_handle_t st7703_io = NULL;
static esp_lcd_panel_handle_t st7703_panel = NULL;
static lv_display_t *disp_handle = NULL;
static lv_indev_t *touch_indev = NULL;
static esp_lcd_touch_handle_t touch_panel = NULL;
static i2c_master_bus_handle_t internal_i2c_bus = NULL;
static esp_ldo_channel_handle_t phy_pwr_chan = NULL;
static esp_ldo_channel_handle_t audio_pwr_chan = NULL;

static int g_backlight_state = 1;
static uint8_t *lv_buf1 = NULL, *lv_buf2 = NULL;
static bool hardware_init_done = false;
static bool g_callbacks_enabled = false;
static void *reserved_fb_ptr = NULL;

// LVGL tick source: provide millisecond time via esp_timer
static uint32_t waveshare_lv_tick_get_cb(void) {
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void touchpad_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  if (touch_panel) {
    uint16_t touch_x[1];
    uint16_t touch_y[1];
    uint16_t touch_strength[1];
    uint8_t touch_cnt = 0;

    esp_lcd_touch_read_data(touch_panel);
    bool touchpad_pressed = esp_lcd_touch_get_coordinates(
        touch_panel, touch_x, touch_y, touch_strength, &touch_cnt, 1);

    if (touchpad_pressed && touch_cnt > 0) {
      data->point.x = touch_x[0];
      data->point.y = touch_y[0];
      data->state = LV_INDEV_STATE_PRESSED;
    } else {
      data->state = LV_INDEV_STATE_RELEASED;
    }
  }
}

static volatile bool flush_pending = false;

static bool display_color_trans_done(esp_lcd_panel_handle_t panel,
                                     esp_lcd_dpi_panel_event_data_t *edata,
                                     void *user_ctx) {
  if (!g_callbacks_enabled) return false;

  if (disp_handle && lv_is_initialized() && flush_pending) {
      lv_display_t * d = lv_display_get_next(NULL);
      bool found = false;
      while (d) {
          if (d == disp_handle) { found = true; break; }
          d = lv_display_get_next(d);
      }
      if (found) {
        flush_pending = false;
        lv_display_flush_ready(disp_handle);
      }
  }
  return false;
}

static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area,
                          uint8_t *px_map) {
  if (g_callbacks_enabled == false) {
      lv_display_flush_ready(disp);
      return;
  }
  
  int offsetx1 = area->x1;
  int offsetx2 = area->x2;
  int offsety1 = area->y1;
  int offsety2 = area->y2;
  
  flush_pending = true;
  esp_lcd_panel_draw_bitmap(st7703_panel, offsetx1, offsety1, offsetx2 + 1,
                            offsety2 + 1, px_map);
}

void waveshare_deinit_internal(void) {
  g_callbacks_enabled = false;
  
  if (hardware_init_done) {
      mp_printf(&mp_plat_print, "mod_waveshare_4: SILENCING Hardware (Early Lock)...\n");
      if (st7703_panel) {
          esp_lcd_dpi_panel_event_callbacks_t null_cbs = {0};
          esp_lcd_dpi_panel_register_event_callbacks(st7703_panel, &null_cbs, NULL);
          esp_lcd_panel_disp_on_off(st7703_panel, false);
      }
      gpio_set_level((gpio_num_t)26, 1); // Backlight Off (Active-Low)

      if (lv_is_initialized() && disp_handle) {
          lv_display_delete(disp_handle);
          disp_handle = NULL;
      }
      touch_indev = NULL;

      if (lv_is_initialized()) {
          lv_deinit();
      }
  }
}

void waveshare_deinit(void) {
    waveshare_deinit_internal();
}

static mp_obj_t waveshare_deinit_obj_func(void) {
    waveshare_deinit_internal();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(waveshare_deinit_module_obj, waveshare_deinit_obj_func);

static mp_obj_t waveshare_init(void) {
  if (!lv_is_initialized()) {
    mp_raise_ValueError("LVGL is NOT initialized. You MUST call lv.init() first!");
    return mp_const_none;
  }

  disp_handle = NULL;

  if (!hardware_init_done) {
    mp_printf(&mp_plat_print, "mod_waveshare_4: Initializing Hardware (Singleton)...\n");

    // 1. Initialize Backlight GPIO (GPIO 26)
    gpio_config_t bk_conf = {.pin_bit_mask = (1ULL << 26),
                             .mode = GPIO_MODE_OUTPUT,
                             .pull_up_en = GPIO_PULLUP_DISABLE,
                             .pull_down_en = GPIO_PULLDOWN_DISABLE,
                             .intr_type = GPIO_INTR_DISABLE,
                             .hys_ctrl_mode = GPIO_HYS_SOFT_ENABLE};
    gpio_config(&bk_conf);

    // P4 MIPI DPHY requires LDO 3 set to 2.5V
    if (phy_pwr_chan == NULL) {
        esp_ldo_channel_config_t ldo_cfg = {
            .chan_id = 3, // MIPI DSI PHY LDO
            .voltage_mv = 2500,
        };
        esp_err_t err = esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan);
        if (err != ESP_OK) {
            mp_printf(&mp_plat_print, "Waveshare 4\": Failed to acquire LDO3 for MIPI DPHY: 0x%x\n", err);
        }
    }

    // Audio Power LDO 4
    if (audio_pwr_chan == NULL) {
        esp_ldo_channel_config_t ldo_cfg = {
            .chan_id = 4,
            .voltage_mv = 3300,
        };
        esp_err_t err = esp_ldo_acquire_channel(&ldo_cfg, &audio_pwr_chan);
        if (err != ESP_OK) {
            mp_printf(&mp_plat_print, "Waveshare 4\": Failed to acquire LDO4 power: 0x%x\n", err);
        }
    }

    // 3. Initialize MIPI DSI Bus
    esp_lcd_dsi_bus_config_t bus_config = ST7703_PANEL_BUS_DSI_2CH_CONFIG();
    esp_err_t err = esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus);
    if (err != ESP_OK) {
        mp_printf(&mp_plat_print, "Waveshare 4\": Failed to create DSI bus: 0x%x\n", err);
    }

    // 4. Initialize LCD IO DBI
    esp_lcd_dbi_io_config_t dbi_config = ST7703_PANEL_IO_DBI_CONFIG();
    err = esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &st7703_io);
    if (err != ESP_OK) {
        mp_printf(&mp_plat_print, "Waveshare 4\": Failed to create panel IO: 0x%x\n", err);
    }

    // 5. Initialize DPI Config (720x720 timing porches)
    esp_lcd_dpi_panel_config_t dpi_config = ST7703_720_720_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
    
    st7703_vendor_config_t vendor_config = {
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
        },
        .flags = {
            .use_mipi_interface = 1,
        },
    };

    esp_lcd_panel_dev_config_t lcd_dev_config = {
        .bits_per_pixel = 16,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .reset_gpio_num = 27, // LCD reset GPIO 27
        .vendor_config = (void *)&vendor_config,
    };

    err = esp_lcd_new_panel_st7703(st7703_io, &lcd_dev_config, &st7703_panel);
    if (err != ESP_OK) {
        mp_printf(&mp_plat_print, "Waveshare 4\": Failed to create panel handle: 0x%x\n", err);
    } else {
        err = esp_lcd_panel_reset(st7703_panel);
        if (err != ESP_OK) {
            mp_printf(&mp_plat_print, "Waveshare 4\": Panel reset failed: 0x%x\n", err);
        }
        err = esp_lcd_panel_init(st7703_panel);
        if (err != ESP_OK) {
            mp_printf(&mp_plat_print, "Waveshare 4\": Panel init failed: 0x%x\n", err);
        }
    }
    
    hardware_init_done = true;
  } else {
      mp_printf(&mp_plat_print, "mod_waveshare_4: Reusing Hardware Singleton\n");
  }

  // Always restore backlight on init (Active-Low: 0=ON, 1=OFF)
  gpio_set_level((gpio_num_t)26, g_backlight_state ? 0 : 1);
  esp_lcd_panel_disp_on_off(st7703_panel, true);
  esp_lcd_panel_io_tx_param(st7703_io, LCD_CMD_DISPON, NULL, 0);
  
  // 6. Connect LVGL 9
  lv_tick_set_cb(waveshare_lv_tick_get_cb);

  if (disp_handle == NULL) {
      disp_handle = lv_display_create(720, 720);
      lv_display_set_flush_cb(disp_handle, disp_flush_cb);
  }

  // Allocate draw buffers once, reuse on re-init
  size_t draw_buffer_sz = 720 * 20 * sizeof(uint16_t);
  if (lv_buf1 == NULL) {
    lv_buf1 =
        heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  }
  if (lv_buf2 == NULL) {
    lv_buf2 =
        heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  }

  lv_display_set_buffers(disp_handle, lv_buf1, lv_buf2, draw_buffer_sz,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  // 7. Initialize Touch (GT911)
  if (touch_panel == NULL) {
    i2c_master_bus_handle_t touch_i2c_bus = internal_i2c_bus;
    esp_lcd_panel_io_handle_t touch_io = NULL;
    esp_lcd_panel_io_i2c_config_t touch_io_config = {
        .dev_addr = 0x5D,
        .scl_speed_hz = 400000,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 16,
        .lcd_param_bits = 8,
        .flags.disable_control_phase = 1,
    };

    // GT911 Reset Sequence using GPIO 23
    gpio_set_direction(23, GPIO_MODE_OUTPUT);
    gpio_set_level(23, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(23, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    if (i2c_master_probe(touch_i2c_bus, 0x5D, 100) != ESP_OK) {
        if (i2c_master_probe(touch_i2c_bus, 0x14, 100) == ESP_OK) {
            touch_io_config.dev_addr = 0x14;
        }
    }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
    esp_lcd_new_panel_io_i2c_v2(touch_i2c_bus, &touch_io_config, &touch_io);
#else
    esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)0, &touch_io_config, &touch_io);
#endif

    esp_lcd_touch_config_t touch_config = {
        .x_max = 720,
        .y_max = 720,
        .rst_gpio_num = (gpio_num_t)-1, // Already reset manually
        .int_gpio_num = (gpio_num_t)-1, // No interrupt pin
        .levels = {.reset = 0, .interrupt = 0},
        .flags = {.swap_xy = 0, .mirror_x = 0, .mirror_y = 0},
    };
    esp_lcd_touch_new_i2c_gt911(touch_io, &touch_config, &touch_panel);
    if (touch_panel) {
        esp_lcd_touch_exit_sleep(touch_panel);
    }
  }

  if (touch_panel) {
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpad_read_cb);
    lv_indev_set_display(indev, disp_handle);
    touch_indev = indev;
  }

  if (st7703_panel) {
      static esp_lcd_dpi_panel_event_callbacks_t cbs = {0};
      cbs.on_color_trans_done = display_color_trans_done;
      esp_lcd_dpi_panel_register_event_callbacks(st7703_panel, &cbs, NULL);
  }

  g_callbacks_enabled = true;

  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(waveshare_init_obj, waveshare_init);

static mp_obj_t waveshare_suspend_display(void) {
  if (!disp_handle) return mp_const_none;
  lv_display_set_flush_cb(disp_handle, NULL);
  lv_display_flush_ready(disp_handle);
  gpio_set_level((gpio_num_t)26, 1); // Backlight Off (Active-Low)
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(waveshare_suspend_display_obj, waveshare_suspend_display);

static mp_obj_t waveshare_resume_display(void) {
  if (!disp_handle) return mp_const_none;
  lv_display_set_flush_cb(disp_handle, disp_flush_cb);
  lv_obj_invalidate(lv_screen_active());
  gpio_set_level((gpio_num_t)26, g_backlight_state ? 0 : 1); // Backlight Restored (Active-Low)
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(waveshare_resume_display_obj, waveshare_resume_display);

static mp_obj_t waveshare_set_backlight(mp_obj_t brightness_obj) {
  int brightness = mp_obj_get_int(brightness_obj);
  g_backlight_state = (brightness > 0) ? 1 : 0;
  gpio_set_level((gpio_num_t)26, g_backlight_state ? 0 : 1); // Backlight set (Active-Low)
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(waveshare_set_backlight_obj, waveshare_set_backlight);

static mp_obj_t waveshare_read_touch(void) {
  if (!touch_panel) return mp_const_none;
  uint16_t x[1], y[1], strength[1];
  uint8_t count = 0;
  esp_lcd_touch_read_data(touch_panel);
  if (esp_lcd_touch_get_coordinates(touch_panel, x, y, strength, &count, 1) && count > 0) {
    mp_obj_t tuple[2] = {mp_obj_new_int(x[0]), mp_obj_new_int(y[0])};
    return mp_obj_new_tuple(2, tuple);
  }
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(waveshare_read_touch_obj, waveshare_read_touch);

static mp_obj_t waveshare_get_info(void) {
  if (!touch_panel) return mp_obj_new_str("Not Initialized", 15);
  uint8_t id[5] = {0}; uint8_t ver = 0;
  esp_lcd_panel_io_rx_param(touch_panel->io, 0x8140, id, 4);
  esp_lcd_panel_io_rx_param(touch_panel->io, 0x8047, &ver, 1);
  char buf[128];
  snprintf(buf, sizeof(buf), "GT911 ID: %s, Config Ver: %d", (char *)id, ver);
  return mp_obj_new_str(buf, strlen(buf));
}
static MP_DEFINE_CONST_FUN_OBJ_0(waveshare_get_info_obj, waveshare_get_info);

static mp_obj_t waveshare_speaker_enable(mp_obj_t enable_obj) {
  gpio_reset_pin((gpio_num_t)53);
  gpio_set_direction((gpio_num_t)53, GPIO_MODE_OUTPUT);
  // Waveshare speaker enable is ACTIVE HIGH (1=ON, 0=OFF)
  gpio_set_level((gpio_num_t)53, mp_obj_is_true(enable_obj) ? 1 : 0);
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(waveshare_speaker_enable_obj, waveshare_speaker_enable);

static mp_obj_t waveshare_beep(mp_obj_t freq_obj, mp_obj_t duration_obj) {
  int freq = mp_obj_get_int(freq_obj);
  int duration_ms = mp_obj_get_int(duration_obj);
  if (freq <= 0) return mp_const_none;
  i2s_chan_handle_t tx_handle = NULL;
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  i2s_new_channel(&chan_cfg, &tx_handle, NULL);
  
  // WS Speaker I2S GPIO layout (MCLK: 13, BCLK: 12, WS: 10, DOUT: 9)
  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
      .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {.mclk = (gpio_num_t)13, .bclk = (gpio_num_t)12, .ws = (gpio_num_t)10, .dout = (gpio_num_t)9, .din = I2S_GPIO_UNUSED},
  };
  std_cfg.slot_cfg.bit_shift = true; std_cfg.slot_cfg.left_align = true;
  i2s_channel_init_std_mode(tx_handle, &std_cfg);
  i2s_channel_enable(tx_handle);
  
  // Enable speaker amplifier (GPIO 53, active high)
  gpio_set_level((gpio_num_t)53, 1);
  
  int period = 44100 / freq; if (period == 0) period = 1; int half = period / 2;
  size_t b_sz = 512; int16_t *b = malloc(b_sz * 2 * sizeof(int16_t));
  if (b) {
    int64_t end = esp_timer_get_time() + (int64_t)duration_ms * 1000;
    int s_idx = 0;
    while (esp_timer_get_time() < end) {
      for (int i = 0; i < b_sz; i++) {
        int16_t val = (s_idx % period < half) ? 4000 : -4000;
        b[i * 2] = val; b[i * 2 + 1] = val; s_idx++;
      }
      size_t written; i2s_channel_write(tx_handle, b, b_sz * 2 * sizeof(int16_t), &written, portMAX_DELAY);
    }
    free(b);
  }
  
  // Disable speaker amplifier (GPIO 53)
  gpio_set_level((gpio_num_t)53, 0);
  
  i2s_channel_disable(tx_handle); i2s_del_channel(tx_handle);
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(waveshare_beep_obj, waveshare_beep);

static const mp_rom_map_elem_t waveshare_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&waveshare_init_obj)},
    {MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&waveshare_deinit_module_obj)},
    {MP_ROM_QSTR(MP_QSTR_get_info), MP_ROM_PTR(&waveshare_get_info_obj)},
    {MP_ROM_QSTR(MP_QSTR_read_touch), MP_ROM_PTR(&waveshare_read_touch_obj)},
    {MP_ROM_QSTR(MP_QSTR_set_backlight), MP_ROM_PTR(&waveshare_set_backlight_obj)},
    {MP_ROM_QSTR(MP_QSTR_suspend_display), MP_ROM_PTR(&waveshare_suspend_display_obj)},
    {MP_ROM_QSTR(MP_QSTR_resume_display), MP_ROM_PTR(&waveshare_resume_display_obj)},
    {MP_ROM_QSTR(MP_QSTR_speaker_enable), MP_ROM_PTR(&waveshare_speaker_enable_obj)},
    {MP_ROM_QSTR(MP_QSTR_beep), MP_ROM_PTR(&waveshare_beep_obj)},
};
static MP_DEFINE_CONST_DICT(waveshare_module_globals, waveshare_module_globals_table);
const mp_obj_module_t mp_module_waveshare = {.base = {&mp_type_module}, .globals = (mp_obj_dict_t *)&waveshare_module_globals};
// Register standard waveshare module name
MP_REGISTER_MODULE(MP_QSTR_waveshare, mp_module_waveshare);

extern void boardctrl_startup(void);
void waveshare_startup(void) {
  printf("Waveshare 4\": Performing WiFi Slave Hardware Reset...\n");
  // 1. Reset WiFi Slave (GPIO 32 is active high per sdkconfig)
  gpio_config_t rst_cfg = {
      .pin_bit_mask = (1ULL << 32),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = 0,
      .pull_down_en = 0,
  };
  gpio_config(&rst_cfg);
  gpio_set_level(32, 1); // Reset ON
  vTaskDelay(pdMS_TO_TICKS(100));
  gpio_set_level(32, 0); // Reset OFF
  vTaskDelay(pdMS_TO_TICKS(500)); // Wait for slave to boot
  printf("Waveshare 4\": Hardware Initialized.\n");

  boardctrl_startup();

  if (audio_pwr_chan == NULL) {
      esp_ldo_channel_config_t ldo4_config = {.chan_id = 4, .voltage_mv = 3300};
      esp_ldo_acquire_channel(&ldo4_config, &audio_pwr_chan);
  }
  if (internal_i2c_bus == NULL) {
      // Main I2C for Waveshare: SDA=7, SCL=8, disable internal pullups
      i2c_master_bus_config_t i2c_bus_cfg = {
          .i2c_port = I2C_NUM_1,
          .sda_io_num = (gpio_num_t)7,
          .scl_io_num = (gpio_num_t)8,
          .clk_source = I2C_CLK_SRC_DEFAULT,
          .glitch_ignore_cnt = 7,
          .flags.enable_internal_pullup = false
      };
      i2c_new_master_bus(&i2c_bus_cfg, &internal_i2c_bus);
  }
  if (reserved_fb_ptr == NULL) {
    reserved_fb_ptr = heap_caps_malloc(1500 * 1024, MALLOC_CAP_SPIRAM);
  }
}
