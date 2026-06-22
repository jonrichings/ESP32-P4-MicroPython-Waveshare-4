/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_lcd_ek79007.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "usb/usb_host.h"

#include "bsp/display.h"
#include "bsp/esp32_p4_function_ev_board.h"
#include "bsp/touch.h"
#include "bsp_err_check.h"
#include "driver/i2c_master.h"
#include "esp_lcd_touch_gt911.h"

static const char *TAG = "ESP32_P4_EV";

#if (BSP_CONFIG_NO_GRAPHIC_LIB == 0)
static lv_indev_t *disp_indev = NULL;
#endif // (BSP_CONFIG_NO_GRAPHIC_LIB == 0)

sdmmc_card_t *bsp_sdcard = NULL;  // Global uSD card handler
static esp_lcd_touch_handle_t tp; // LCD touch handle
static bool i2c_initialized = false;
static TaskHandle_t usb_host_task; // USB Host Library task

i2c_master_bus_handle_t bsp_i2c_bus_handle = NULL;

esp_err_t bsp_i2c_init(void) {
  /* I2C was initialized before */
  if (i2c_initialized) {
    return ESP_OK;
  }

  i2c_master_bus_config_t i2c_bus_config = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .i2c_port = BSP_I2C_NUM,
      .scl_io_num = BSP_I2C_SCL,
      .sda_io_num = BSP_I2C_SDA,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
  };
  BSP_ERROR_CHECK_RETURN_ERR(
      i2c_new_master_bus(&i2c_bus_config, &bsp_i2c_bus_handle));

  i2c_initialized = true;

  return ESP_OK;
}

esp_err_t bsp_i2c_deinit(void) {
  if (bsp_i2c_bus_handle) {
    BSP_ERROR_CHECK_RETURN_ERR(i2c_del_master_bus(bsp_i2c_bus_handle));
    bsp_i2c_bus_handle = NULL;
  }
  i2c_initialized = false;
  return ESP_OK;
}

esp_err_t bsp_sdcard_mount(void) {
  const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
#ifdef CONFIG_BSP_SD_FORMAT_ON_MOUNT_FAIL
      .format_if_mount_failed = true,
#else
      .format_if_mount_failed = false,
#endif
      .max_files = 5,
      .allocation_unit_size = 64 * 1024};

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = SDMMC_HOST_SLOT_0;
  host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.cd = SDMMC_SLOT_NO_CD;
  slot_config.wp = SDMMC_SLOT_NO_WP;
  slot_config.width = 1;

  // CRITICAL: Slot 0 default config maps all 8 data lines.
  // We MUST mask out D1-D7 to prevent the SD driver from stealing
  // Touch Panel signals (40, 42) and the I2C Bus signals (45, 46)!
  slot_config.d1 = GPIO_NUM_NC;
  slot_config.d2 = GPIO_NUM_NC;
  slot_config.d3 = GPIO_NUM_NC;
  slot_config.d4 = GPIO_NUM_NC;
  slot_config.d5 = GPIO_NUM_NC;
  slot_config.d6 = GPIO_NUM_NC;
  slot_config.d7 = GPIO_NUM_NC;
  slot_config.flags = 0;

  return esp_vfs_fat_sdmmc_mount(BSP_SD_MOUNT_POINT, &host, &slot_config,
                                 &mount_config, &bsp_sdcard);
}

esp_err_t bsp_sdcard_unmount(void) {
  return esp_vfs_fat_sdcard_unmount(BSP_SD_MOUNT_POINT, bsp_sdcard);
}

esp_err_t bsp_spiffs_mount(void) {
  esp_vfs_spiffs_conf_t conf = {
      .base_path = CONFIG_BSP_SPIFFS_MOUNT_POINT,
      .partition_label = CONFIG_BSP_SPIFFS_PARTITION_LABEL,
      .max_files = CONFIG_BSP_SPIFFS_MAX_FILES,
#ifdef CONFIG_BSP_SPIFFS_FORMAT_ON_MOUNT_FAIL
      .format_if_mount_failed = true,
#else
      .format_if_mount_failed = false,
#endif
  };

  esp_err_t ret_val = esp_vfs_spiffs_register(&conf);

  BSP_ERROR_CHECK_RETURN_ERR(ret_val);

  size_t total = 0, used = 0;
  ret_val = esp_spiffs_info(conf.partition_label, &total, &used);
  if (ret_val != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)",
             esp_err_to_name(ret_val));
  } else {
    ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
  }

  return ret_val;
}

esp_err_t bsp_spiffs_unmount(void) {
  return esp_vfs_spiffs_unregister(CONFIG_BSP_SPIFFS_PARTITION_LABEL);
}

// Bit number used to represent command and parameter
#define LCD_LEDC_CH CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH

static esp_err_t bsp_display_brightness_init(void) {
  // Bypassing LEDC. Configure GPIO 31 as a standard digital output pin.
  gpio_config_t bk_conf = {.pin_bit_mask = (1ULL << BSP_LCD_BACKLIGHT),
                           .mode = GPIO_MODE_OUTPUT,
                           .pull_up_en = GPIO_PULLUP_DISABLE,
                           .pull_down_en = GPIO_PULLDOWN_DISABLE,
                           .intr_type = GPIO_INTR_DISABLE};
  return gpio_config(&bk_conf);
}

esp_err_t bsp_display_brightness_set(int brightness_percent) {
  if (brightness_percent > 100) {
    brightness_percent = 100;
  }
  if (brightness_percent < 0) {
    brightness_percent = 0;
  }

  ESP_LOGI(TAG, "Setting LCD backlight: %d%%", brightness_percent);
  // Bypassing LEDC. Simple digital on/off.
  if (brightness_percent > 0) {
    return gpio_set_level(BSP_LCD_BACKLIGHT, 1);
  } else {
    return gpio_set_level(BSP_LCD_BACKLIGHT, 0);
  }
}

esp_err_t bsp_display_backlight_off(void) {
  return bsp_display_brightness_set(0);
}

esp_err_t bsp_display_backlight_on(void) {
  return bsp_display_brightness_set(100);
}

static esp_err_t bsp_enable_dsi_phy_power(void) {
#if BSP_MIPI_DSI_PHY_PWR_LDO_CHAN > 0
  // Turn on the power for MIPI DSI PHY, so it can go from "No Power" state to
  // "Shutdown" state
  static esp_ldo_channel_handle_t phy_pwr_chan = NULL;
  esp_ldo_channel_config_t ldo_cfg = {
      .chan_id = BSP_MIPI_DSI_PHY_PWR_LDO_CHAN,
      .voltage_mv = BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
  };
  ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan), TAG,
                      "Acquire LDO channel for DPHY failed");
  ESP_LOGI(TAG, "MIPI DSI PHY Powered on");
#endif // BSP_MIPI_DSI_PHY_PWR_LDO_CHAN > 0

  // Power on LCD Logic via LDO4 (3300mV) before DPI initialization.
  // This also globally powers the SD Card Slot.
  static esp_ldo_channel_handle_t ldo4_handle = NULL;
  esp_ldo_channel_config_t ldo4_config = {
      .chan_id = 4,
      .voltage_mv = 3300,
  };
  ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo4_config, &ldo4_handle), TAG,
                      "failed to acquire LDO4 channel for LCD/SD power");
  ESP_LOGI(TAG, "LDO4 (3300mV) Powered On for LCD and SD Card");

  return ESP_OK;
}

esp_err_t bsp_display_new(const bsp_display_config_t *config,
                          esp_lcd_panel_handle_t *ret_panel,
                          esp_lcd_panel_io_handle_t *ret_io) {
  esp_err_t ret = ESP_OK;
  bsp_lcd_handles_t handles;
  ret = bsp_display_new_with_handles(config, &handles);

  *ret_panel = handles.panel;
  *ret_io = handles.io;

  return ret;
}

esp_err_t bsp_display_new_with_handles(const bsp_display_config_t *config,
                                       bsp_lcd_handles_t *ret_handles) {
  esp_err_t ret = ESP_OK;
  esp_lcd_panel_io_handle_t io = NULL;
  esp_lcd_panel_handle_t ek79007_panel = NULL;
  esp_lcd_panel_handle_t ek79007_ctrl_panel = NULL;
  esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;

  ESP_RETURN_ON_ERROR(bsp_display_brightness_init(), TAG,
                      "Brightness init failed");
  ESP_RETURN_ON_ERROR(bsp_enable_dsi_phy_power(), TAG, "DSI PHY power failed");

  /* create MIPI DSI bus first, it will initialize the DSI PHY as well */
  esp_lcd_dsi_bus_config_t bus_config = {
      .bus_id = 0,
      .num_data_lanes = BSP_LCD_MIPI_DSI_LANE_NUM,
      .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
      .lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS,
  };
  ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus), TAG,
                      "New DSI bus init failed");

  ESP_LOGI(TAG, "Install MIPI DSI LCD control panel");
  // we use DBI interface to send LCD commands and parameters
  esp_lcd_dbi_io_config_t dbi_config = {
      .virtual_channel = 0,
      .lcd_cmd_bits = 8,   // according to the LCD EK79007 spec
      .lcd_param_bits = 8, // according to the LCD EK79007 spec
  };
  ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io),
                    err, TAG, "New panel IO failed");

  ESP_LOGI(TAG, "Install MIPI DSI LCD data panel");
  static esp_lcd_dpi_panel_config_t dpi_config = {
      .virtual_channel = 0,
      .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
      .dpi_clock_freq_mhz = BSP_LCD_PIXEL_CLOCK_MHZ,
      .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
      .video_timing =
          {
              .h_size = BSP_LCD_H_RES,
              .v_size = BSP_LCD_V_RES,
              .hsync_back_porch = BSP_LCD_MIPI_DSI_LCD_HBP,
              .hsync_pulse_width = BSP_LCD_MIPI_DSI_LCD_HSYNC,
              .hsync_front_porch = BSP_LCD_MIPI_DSI_LCD_HFP,
              .vsync_back_porch = BSP_LCD_MIPI_DSI_LCD_VBP,
              .vsync_pulse_width = BSP_LCD_MIPI_DSI_LCD_VSYNC,
              .vsync_front_porch = BSP_LCD_MIPI_DSI_LCD_VFP,
          },
      .flags.use_dma2d = true,
  };

  static ek79007_vendor_config_t vendor_config = {
      .init_cmds = NULL,
      .init_cmds_size = 0,
      .mipi_config =
          {
              .dsi_bus = NULL,
              .dpi_config = &dpi_config,
              .lane_num = BSP_LCD_MIPI_DSI_LANE_NUM,
          },
  };
  vendor_config.mipi_config.dsi_bus = mipi_dsi_bus;

  // create EK79007 control panel
  esp_lcd_panel_dev_config_t lcd_dev_config = {
      .bits_per_pixel = 16,
      .rgb_ele_order = BSP_LCD_COLOR_SPACE,
      .reset_gpio_num = -1, // Use -1 to let PMU/Hardware RC manage the reset
      .vendor_config = (void *)&vendor_config,
  };
  ESP_GOTO_ON_ERROR(
      esp_lcd_new_panel_ek79007(io, &lcd_dev_config, &ek79007_ctrl_panel), err,
      TAG, "New LCD panel EK79007 failed");
  ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(ek79007_ctrl_panel), err, TAG,
                    "LCD panel reset failed");
  ESP_GOTO_ON_ERROR(esp_lcd_panel_init(ek79007_ctrl_panel), err, TAG,
                    "LCD panel init failed");

  // Explicitly send the LCD_CMD_DISPON (0x29) since esp_lcd_panel_disp_on_off
  // is not supported by driver
  esp_lcd_panel_io_tx_param(io, LCD_CMD_DISPON, NULL, 0);

  // The EK79007 driver creates the DPI panel internally and returns it as the
  // control panel. So we just assign the control panel to the data panel.
  ek79007_panel = ek79007_ctrl_panel;

  /* Return all handles */
  ret_handles->io = io;
  ret_handles->mipi_dsi_bus = mipi_dsi_bus;
  ret_handles->panel = ek79007_panel;
  ret_handles->control = ek79007_ctrl_panel;

  ESP_LOGI(TAG, "Display initialized");

  return ret;

err:
  if (ek79007_panel) {
    esp_lcd_panel_del(ek79007_panel);
  }
  if (ek79007_ctrl_panel) {
    esp_lcd_panel_del(ek79007_ctrl_panel);
  }
  if (io) {
    esp_lcd_panel_io_del(io);
  }
  if (mipi_dsi_bus) {
    esp_lcd_del_dsi_bus(mipi_dsi_bus);
  }
  return ret;
}

esp_err_t bsp_touch_new(const bsp_touch_config_t *config,
                        esp_lcd_touch_handle_t *ret_touch) {
  /* Initilize I2C */
  BSP_ERROR_CHECK_RETURN_ERR(bsp_i2c_init());

  /* Initialize touch */
  esp_lcd_touch_config_t tp_cfg = {
      .x_max = BSP_LCD_H_RES,
      .y_max = BSP_LCD_V_RES,
      .rst_gpio_num =
          GPIO_NUM_NC, // Handled by LCD display driver (shared logic)
      .int_gpio_num = BSP_LCD_TOUCH_INT,
      .levels =
          {
              .reset = 0,
              .interrupt = 0,
          },
      .flags =
          {
              .mirror_x = 0,
              .mirror_y = 0,
          },
  };
  esp_lcd_panel_io_handle_t tp_io_handle = NULL;
  esp_lcd_panel_io_i2c_config_t tp_io_config =
      ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
  tp_io_config.scl_speed_hz = CONFIG_BSP_I2C_CLK_SPEED_HZ;

  if (i2c_master_probe(bsp_i2c_bus_handle, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
                       1000) == ESP_OK) {
    tp_io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    ESP_LOGI(TAG, "GT911 touch panel found at address 0x%02X",
             (unsigned int)tp_io_config.dev_addr);
  } else if (i2c_master_probe(bsp_i2c_bus_handle,
                              ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP,
                              1000) == ESP_OK) {
    tp_io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
    ESP_LOGI(TAG, "GT911 touch panel found at backup address 0x%02X",
             (unsigned int)tp_io_config.dev_addr);
  } else {
    ESP_LOGE(TAG, "GT911 touch panel NOT found on I2C bus!");
  }

  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bsp_i2c_bus_handle,
                                               &tp_io_config, &tp_io_handle),
                      TAG, "");
  return esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, ret_touch);
}

#if (BSP_CONFIG_NO_GRAPHIC_LIB == 0)
static lv_display_t *bsp_display_lcd_init(const bsp_display_cfg_t *cfg) {
  assert(cfg != NULL);
  bsp_lcd_handles_t lcd_panels;
  BSP_ERROR_CHECK_RETURN_NULL(bsp_display_new_with_handles(NULL, &lcd_panels));

  /* Add LCD screen */
  ESP_LOGD(TAG, "Add LCD screen");
  const lvgl_port_display_cfg_t disp_cfg = {
      .io_handle = lcd_panels.io,
      .panel_handle = lcd_panels.panel,
      .control_handle = lcd_panels.control,
      .buffer_size = cfg->buffer_size,
      .double_buffer = cfg->double_buffer,
      .hres = BSP_LCD_H_RES,
      .vres = BSP_LCD_V_RES,
      .monochrome = false,
      /* Rotation values must be same as used in esp_lcd for initial settings of
         the screen */
      .rotation =
          {
              .mirror_x = false,
              .mirror_y = false,
          },
      .flags = {
          .buff_dma = cfg->flags.buff_dma,
          .buff_spiram = cfg->flags.buff_spiram,
          .sw_rotate = true,
#if LVGL_VERSION_MAJOR >= 9
          .swap_bytes = (BSP_LCD_BIGENDIAN ? true : false),
#endif
      }};

  static const lvgl_port_display_dsi_cfg_t dsi_cfg = {
      .flags = {
          .avoid_tearing = false,
      }};

  return lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
}

static lv_indev_t *bsp_display_indev_init(lv_display_t *disp) {
  BSP_ERROR_CHECK_RETURN_NULL(bsp_touch_new(NULL, &tp));
  assert(tp);

  /* Add touch input (for selected screen) */
  const lvgl_port_touch_cfg_t touch_cfg = {
      .disp = disp,
      .handle = tp,
  };

  return lvgl_port_add_touch(&touch_cfg);
}

lv_display_t *bsp_display_start(void) {
  bsp_display_cfg_t cfg = {.lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
                           .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
                           .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
                           .flags = {
                               .buff_dma = false,
                               .buff_spiram = true,
                           }};
  return bsp_display_start_with_config(&cfg);
}

lv_display_t *bsp_display_start_with_config(const bsp_display_cfg_t *cfg) {
  lv_display_t *disp;

  assert(cfg != NULL);
  BSP_ERROR_CHECK_RETURN_NULL(lvgl_port_init(&cfg->lvgl_port_cfg));

  BSP_ERROR_CHECK_RETURN_NULL(bsp_display_brightness_init());

  BSP_NULL_CHECK(disp = bsp_display_lcd_init(cfg), NULL);

  BSP_NULL_CHECK(disp_indev = bsp_display_indev_init(disp), NULL);

  return disp;
}

lv_indev_t *bsp_display_get_input_dev(void) { return disp_indev; }

void bsp_display_rotate(lv_display_t *disp, lv_disp_rotation_t rotation) {
  lv_disp_set_rotation(disp, rotation);
}

bool bsp_display_lock(uint32_t timeout_ms) {
  return lvgl_port_lock(timeout_ms);
}

void bsp_display_unlock(void) { lvgl_port_unlock(); }

#endif // (BSP_CONFIG_NO_GRAPHIC_LIB == 0)

static void usb_lib_task(void *arg) {
  while (1) {
    // Start handling system events
    uint32_t event_flags;
    usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
      ESP_ERROR_CHECK(usb_host_device_free_all());
    }
    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
      ESP_LOGI(TAG, "USB: All devices freed");
      // Continue handling USB events to allow device reconnection
      // The only way this task can be stopped is by calling bsp_usb_host_stop()
    }
  }
}

esp_err_t bsp_usb_host_start(bsp_usb_host_power_mode_t mode, bool limit_500mA) {
  // Install USB Host driver. Should only be called once in entire application
  ESP_LOGI(TAG, "Installing USB Host");
  const usb_host_config_t host_config = {
      .skip_phy_setup = false,
      .intr_flags = ESP_INTR_FLAG_LEVEL1,
  };
  BSP_ERROR_CHECK_RETURN_ERR(usb_host_install(&host_config));

  // Create a task that will handle USB library events
  if (xTaskCreate(usb_lib_task, "usb_lib", 4096, NULL, 10, &usb_host_task) !=
      pdTRUE) {
    ESP_LOGE(TAG, "Creating USB host lib task failed");
    abort();
  }

  return ESP_OK;
}

esp_err_t bsp_usb_host_stop(void) {
  usb_host_uninstall();
  if (usb_host_task) {
    vTaskSuspend(usb_host_task);
    vTaskDelete(usb_host_task);
  }
  return ESP_OK;
}
