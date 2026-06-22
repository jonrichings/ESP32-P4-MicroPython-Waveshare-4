# ESP32-P4 HMI Board Porting & Stabilization Guide

This guide compiles the key findings, workarounds, and configuration settings discovered during the development of the Elecrow ESP32-P4 Advanced Phone MicroPython port. Use this reference when setting up a new HMI board with a similar design.

---

## 1. Silicon Revision & Flashing Workarounds (Rev 0 / ECO2)

If your new board also uses a **Revision 0 (ECO2)** ESP32-P4 chip, the following configurations are mandatory to ensure stable flashing and startup:

### In `sdkconfig.board` (or via menuconfig):
```ini
# Forces compatibility with Revision 0 silicon
CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y
CONFIG_ESP32P4_REV_MIN_0=y
```

---

## 2. SDIO Bus & WiFi/Bluetooth Bridge Stability

The `esp-hosted` SDIO link on Rev 0 silicon is highly sensitive to timing jitter and high-speed bus congestion (such as concurrency with MIPI display activity). 

### The Workaround Checklist:
1. **Reduce SDIO Clock Speed**: Set the SDIO clock speed to **20MHz** instead of the default 40MHz.
   ```ini
   CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ=20000
   ```
2. **Increase Slave Boot Delay**: Provide at least **2000ms** to allow the ESP32-C6 coprocessor to boot and stabilize before the host attempts communications.
   ```ini
   CONFIG_ESP_HOSTED_SDIO_RESET_DELAY_MS=2000
   ```
3. **GPIO Reset Line**: Keep `CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE=32` (or the respective hardware reset pin for your new board's network slave) and make sure it is physically toggled at startup.

### In C Code (Board Startup Phase):
Physically pulse the WiFi slave reset line active-high (or active-low depending on the new schematic) before initializing the bridge driver:
```c
// Toggle Reset (GPIO 32)
gpio_set_level(32, 1); // Reset ON
vTaskDelay(pdMS_TO_TICKS(100));
gpio_set_level(32, 0); // Reset OFF
vTaskDelay(pdMS_TO_TICKS(500)); // Let the slave boot
```

---

## 3. RTOS & Bluetooth Core 1 Panic Prevention

Concurrent WiFi and NimBLE Bluetooth usage can cause Core 1 panics (`Load access fault`) due to MicroPython's garbage collector scanning timing.

### The Workarounds:
1. **NimBLE Pointer Registration**: Ensure the `bluetooth_nimble_root_pointers` are registered in the main port initialization (`ports/esp32/main.c`) rather than late in peripheral C modules. This prevents the scanner from missing pointer references during task context-switching.
2. **Task Stack Sizes**: Increase task stacks in `sdkconfig.board` to prevent overflows:
   ```ini
   CONFIG_LWIP_TCPIP_TASK_STACK_SIZE=8192
   CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=8192
   CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=4096
   ```

---

## 4. Porting Peripherals to the New HMI Board

When mapping the new board's hardware, update these sections in your port's C driver (e.g., `mod_hmi.c`):

### A. Display (MIPI DSI)
* **LDO Power**: MIPI DSI PHY power must be explicitly enabled. Usually requires acquiring LDO3 at **2500mV**.
  ```c
  esp_ldo_channel_config_t ldo_cfg = {
      .chan_id = 3,
      .voltage_mv = 2500,
  };
  esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan);
  ```
* **Timing & Resolution**: Map the DPI clock source, pixel format (`RGB565`), and resolution/blanking parameters according to the new LCD panel datasheet.

### B. Touch (GT911 or similar)
* **I2C Address**: GT911 usually operates on `0x5D` or `0x14`.
* **Initialization Sequence**: Toggling the reset/interrupt pins determines the I2C address. Ensure your initialization mirrors this timing:
  ```c
  gpio_set_direction(40, GPIO_MODE_OUTPUT); // Reset Pin
  gpio_set_direction(42, GPIO_MODE_OUTPUT); // INT Pin
  gpio_set_level(40, 0);
  gpio_set_level(42, 0);
  vTaskDelay(pdMS_TO_TICKS(20));
  gpio_set_level(40, 1);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_direction(42, GPIO_MODE_INPUT); // Release INT
  vTaskDelay(pdMS_TO_TICKS(50));
  ```

### C. Backlight (PWM or Output)
* Backlight control is usually handled by `GPIO 31` (on/off) or configured via a PWM channel on the STC8 co-processor/ESP32-P4 LEDC.

### D. Audio Amplifier
* **LDO Power**: Onboard amp chips typically need 3.3V power (acquired via LDO4 at **3300mV**).
* **Amp Enable**: Double check if the mute/enable GPIO pin is active-low (e.g., `GPIO 30` on Elecrow) or active-high.
* **I2S Configuration**: I2S output uses standard pins (`SCK`, `WS`, `SD`). On the Elecrow board these were `22`, `21`, and `23`.

---

## 5. Deployment & REPL Verification

Once the firmware builds:
1. Confirm the serial connection (`COM` port) via `mpremote devs`.
2. Run standard diagnostic scripts locally without flashing to test components:
   * **Touch Check**: `mpremote run touch_diag.py` (tracks tap coordinates).
   * **Audio Test**: `mpremote run play_wav.py` (verifies I2S timing and amplifier output).
   * **Network Test**: `mpremote run web_clock.py` (verifies WiFi stability and screen updates under load).
