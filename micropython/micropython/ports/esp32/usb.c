/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "usb.h"
#include "py/mphal.h"
#include "py/runtime.h"

#include "mpconfigport.h"

#if CONFIG_USB_OTG_SUPPORTED && MICROPY_HW_ENABLE_USBDEV &&                    \
    !CONFIG_ESP_CONSOLE_USB_CDC && !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG

#include "esp_timer.h"
#ifndef NO_QSTR
#ifndef CONFIG_TINYUSB_CDC_ENABLED
#define CONFIG_TINYUSB_CDC_ENABLED 1
#endif
#ifndef CONFIG_TINYUSB_CDC_RX_BUFSIZE
#define CONFIG_TINYUSB_CDC_RX_BUFSIZE 256
#endif

// Include TinyUSB headers after defining the necessary macros
#include "tinyusb.h"
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
#include "tinyusb_cdc_acm.h"
#include "tusb.h"
#else
#include "tusb_cdc_acm.h"
#endif
#endif

#ifndef TINYUSB_USBDEV_0
#define TINYUSB_USBDEV_0 0
#endif

#define CDC_ITF TINYUSB_CDC_ACM_0

static uint8_t usb_rx_buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE];

// This is called from FreeRTOS task "tusb_tsk" in espressif__esp_tinyusb (not
// an ISR).
static void usb_callback_rx(int itf, cdcacm_event_t *event) {
  // espressif__esp_tinyusb places tinyusb rx data onto freertos ringbuffer
  // which this function forwards onto our stdin_ringbuf.
  for (;;) {
    size_t len = 0;
    esp_err_t ret =
        tinyusb_cdcacm_read(itf, usb_rx_buf, sizeof(usb_rx_buf), &len);
    if (ret != ESP_OK) {
      break;
    }
    if (len == 0) {
      break;
    }
    for (size_t i = 0; i < len; ++i) {
      if (usb_rx_buf[i] == mp_interrupt_char) {
        mp_sched_keyboard_interrupt();
      } else {
        ringbuf_put(&stdin_ringbuf, usb_rx_buf[i]);
      }
    }
    mp_hal_wake_main_task();
  }
}

void usb_init(void) {
  // Initialise the USB with defaults.
  tinyusb_config_t tusb_cfg = {0};
  ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

  // Initialise the USB serial interface.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
  tinyusb_config_cdcacm_t acm_cfg = {.cdc_port = CDC_ITF,
                                     .callback_rx = &usb_callback_rx,
                                     .callback_rx_wanted_char = NULL,
                                     .callback_line_state_changed = NULL,
                                     .callback_line_coding_changed = NULL};
#else
  tinyusb_config_cdcacm_t acm_cfg = {.usb_dev = TINYUSB_USBDEV_0,
                                     .cdc_port = CDC_ITF,
                                     .rx_unread_buf_sz = 256,
                                     .callback_rx = &usb_callback_rx,
                                     .callback_rx_wanted_char = NULL,
                                     .callback_line_state_changed = NULL,
                                     .callback_line_coding_changed = NULL};
#endif
#ifdef MICROPY_HW_USB_CUSTOM_RX_WANTED_CHAR_CB
  acm_cfg.callback_rx_wanted_char = &MICROPY_HW_USB_CUSTOM_RX_WANTED_CHAR_CB;
#endif
#ifdef MICROPY_HW_USB_CUSTOM_LINE_STATE_CB
  acm_cfg.callback_line_state_changed = &MICROPY_HW_USB_CUSTOM_LINE_STATE_CB;
#endif
#ifdef MICROPY_HW_USB_CUSTOM_LINE_CODING_CB
  acm_cfg.callback_line_coding_changed = &MICROPY_HW_USB_CUSTOM_LINE_CODING_CB;
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
  ESP_ERROR_CHECK(tinyusb_cdcacm_init(&acm_cfg));
#else
  ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg));
#endif
}

void usb_tx_strn(const char *str, size_t len) {
  // Write out the data to the CDC interface, but only while the USB host is
  // connected.
  uint64_t timeout = esp_timer_get_time() +
                     (uint64_t)(MICROPY_HW_USB_CDC_TX_TIMEOUT_MS * 1000);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
  while (len && esp_timer_get_time() < timeout) {
    size_t l = tinyusb_cdcacm_write_queue(CDC_ITF, (uint8_t *)str, len);
    if (l == 0) {
      tinyusb_cdcacm_write_flush(CDC_ITF, 0);
    } else {
      str += l;
      len -= l;
    }
  }
  tinyusb_cdcacm_write_flush(CDC_ITF, 0);
#else
  while (tud_cdc_n_connected(CDC_ITF) && len &&
         esp_timer_get_time() < timeout) {
    size_t l = tinyusb_cdcacm_write_queue(CDC_ITF, (uint8_t *)str, len);
    if (l == 0) {
      tud_cdc_n_write_flush(CDC_ITF);
    } else {
      str += l;
      len -= l;
    }
  }
  tud_cdc_n_write_flush(CDC_ITF);
#endif
}

#endif // CONFIG_USB_OTG_SUPPORTED && !CONFIG_ESP_CONSOLE_USB_CDC &&
       // !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
