# Integrated System Stabilization Walkthrough

We have successfully stabilized the Elecrow P4 Advanced Phone firmware to handle concurrent WiFi, Bluetooth, and high-resolution Display activity without crashes or intrusive flickering.

## Changes Made

### 1. Fixed Core 1 Crash (Bluetooth)
- Moved the `bluetooth_nimble_root_pointers` registration from `mpnimbleport.c` to `main.c`. 
- This ensures the MicroPython build system correctly scans and registers the NimBLE state objects, preventing NULL pointer dereferences in background tasks.

### 2. Eliminated Display Blackout
- Removed the `elecrow.suspend_display()` and `elecrow.resume_display()` calls during the NTP synchronization in `web_clock.py`.
- The display now remains 100% visible throughout the process.

### 3. Stabilized WiFi/Bluetooth Bridge (SDIO)
- **Reduced Bus Speed**: Lowered the `esp-hosted` SDIO clock from 40MHz to **20MHz** in `sdkconfig.board`. This provides higher timing margins on the Rev 0 silicon.
- **Hardware Reset**: Added code to `mod_elecrow.c` to physically toggle the WiFi slave's reset pin (**GPIO 32**) at boot. This ensures the radio chip is in a clean state after every soft reset.

### 4. Integrated Stress Test
- Configured `web_clock.py` to perform an NTP sync and BLE update every **60 seconds**.
- Verified that the system remains responsive and the UI updates smoothly without "Wifi Unknown Error" or BLE timeouts.

## Future-Proofing (Rev 3+ / ECO3)

While these workarounds are essential for the Rev 0 (ECO2) hardware, newer revisions (Rev 3+) may allow:
- Returning the SDIO clock to **40MHz**.
- Eliminating the need for aggressive MIPI/PSRAM isolation.
- Smoother concurrent operation without any sub-second display blinks or pauses.

For more details, see the [p4_stabilization_guide.md](file:///C:/Users/jonri/.gemini/antigravity/brain/1021964b-e168-4ded-8b0e-02ad1a90a76f/p4_stabilization_guide.md).

## Waveshare P4 (8") Board Integration & Bugfixes

We successfully completed the support and testing of the **Waveshare ESP32-P4 (8-inch)** board:

### 1. C-Level SDMMC Host Sharing (WiFi & Micro SD card coexistence)
- **Problem**: When ESP-Hosted (WiFi/Bluetooth bridge) is running on SDMMC Slot 1, MicroPython's `machine.SDCard` initialization throws `ESP_ERR_INVALID_STATE` (-259) because the host controller is already initialized.
- **Solution**: Patched [machine_sdcard.c](file:///g:/My%20Drive/ODD/Volu%20Sol/ESP32-P4%20MicroPython/micropython/micropython/ports/esp32/machine_sdcard.c) to handle a shared host:
  - If `self->host.init()` returns `ESP_ERR_INVALID_STATE`, a new `SDCARD_CARD_FLAGS_HOST_SHARED` flag is set.
  - The driver bypasses the error and proceeds with slot-level card configuration.
  - During SDCard de-initialization or garbage collection, the host driver teardown is skipped for shared hosts, preserving the active WiFi connection.

### 2. Standalone Display Init Protection
- **Problem**: Running scripts that initialize the display (like `board.init()`) outside the main application caused Core 1 panics (Store access faults) if `lv.init()` was not called first.
- **Solution**: Updated [play_wav.py](file:///g:/My%20Drive/ODD/Volu%20Sol/ESP32-P4%20MicroPython/play_wav.py) and other diagnostics to safely call `lv.init()` before `board.init()`.

### 3. Dynamic Camera & Audio Detection
- **Camera**: Updated [camera.py](file:///g:/My%20Drive/ODD/Volu%20Sol/ESP32-P4%20MicroPython/camera.py) to dynamically query both **OV5647** (used on Waveshare, sharing I2C 0 on pins 7/8 at address `0x36`) and **SC2336** (used on Elecrow, on I2C 1 on pins 12/13 at address `0x30`).
- **Audio**: Updated [play_wav.py](file:///g:/My%20Drive/ODD/Volu%20Sol/ESP32-P4%20MicroPython/play_wav.py) to dynamically use correct I2S pins based on the imported board:
  - Waveshare P4: SCK=12, WS=10, SD=9 (using `waveshare` module)
  - Elecrow P4: SCK=22, WS=21, SD=23 (using `elecrow` module)
- **Micro SD card slot**: Corrected the SDCard slot selection to `slot=0` (which routes to the physical onboard slot on both boards) in [play_wav.py](file:///g:/My%20Drive/ODD/Volu Sol/ESP32-P4%20MicroPython/play_wav.py).

---

## Verification Results

### NTP Sync
The terminal shows regular, successful syncs:
```text
Periodic NTP sync...
Checking WiFi...
Syncing NTP (Background)...
NTP Synced: (2026, 3, 12, 16, 35, ...)
```

### Display Stability
- The UI (LVGL) continues to tick at 60Hz.
- No flickering or "blackout" periods observed during network bursts.

### Radio Coexistence
- BLE advertising is active.
- WiFi maintains connection for the 1-minute updates.

### 1. Audio Playback Test (`play_wav.py`)
Initially, streaming occurred without errors, but the speaker remained silent because the onboard ES8311 audio codec starts up muted and in standby mode. 

We successfully resolved this by:
* **Writing an I2C initialization routine** for the ES8311 (default address `0x18`) to configure power domains, unmute the DAC (`Reg 0x31 -> 0x00`), and set the volume register.
* **Overcoming the missing MCLK signal**: Since MicroPython's standard `I2S` driver on the ESP32-P4 does not output a Master Clock (MCLK) on GPIO 13, we configured the ES8311's internal PLL to use the `SCLK` (Bit Clock / Pin 12) line as the master clock source (`Reg 0x01 -> 0xBF`).
* **Enabling Hardware Volume Control**: Set the DAC volume register `0x32` dynamically based on the volume float parameter (mapping `0.0-1.0` to `0x00-0xDF`), which avoids CPU-heavy sample scaling in MicroPython loops.

We modularized this into a standalone library [audio_setup.py](file:///g:/My%20Drive/ODD/Volu%20Sol/ESP32-P4%20MicroPython/audio_setup.py), and updated [play_wav.py](file:///g:/My%20Drive/ODD/Volu%20Sol/ESP32-P4%20MicroPython/play_wav.py) to import it.

Testing with a WAV file on the SD card:
```text
Board detected: Waveshare P4
mod_waveshare: Reusing Hardware Singleton
SD card mounted.
Codec ES8311 initialized successfully.
File Info: 48000Hz, 16-bit, 1 channel(s)
Hardware/Software Volume: 30%
Finished.
Speaker amplifier disabled.
```
*Result:* Clear, audible audio playback on the Waveshare board with minimal CPU overhead.

### 2. Camera Diagnostics (`camera.py`)
Ran the diagnostic script:
```text
Camera: OV5647 sensor found on address 0x36 (SDA:7, SCL:8)!
```
*Result:* The OV5647 sensor was successfully discovered on the shared I2C bus 0 (SDA:7, SCL:8) at address `0x36`.

### 3. Camera Live Preview Fixes (Orientation, Color, Brightness)
We resolved the monochrome, grainy, and upside-down camera live preview stream on the **Waveshare ESP32-P4 (8-inch)** board:
* **Added Orientation & Exposure Controls**: Exposed `hmirror`, `vflip`, and `exposure` keyword arguments in MicroPython's C `camera.init()` function.
* **Mapped Bayer Pattern to ISP Register**: In the ESP32-P4 ISP hardware, the demosaicing block must be configured with the correct Bayer layout of the sensor. When the sensor is mirrored and flipped, its readout direction changes, transforming the bayer pattern. We implemented an automatic phase calculation in `mod_camera.c`:
  ```c
  int final_bayer_type = bayer_type ^ (vflip << 1) ^ hmirror;
  uint32_t bayer_mode = 3 - final_bayer_type;
  ```
  We write `bayer_mode` directly to the `ISP_FRAME_CFG_REG` (using ESP-IDF's standard `REG_SET_FIELD` macro), which instantly restores full-color rendering and eliminates demosaicing grain/noise.
* **Corrected OV5647 Default Bayer Pattern**: 
  - The OV5647 camera sensor driver natively registers the default Bayer pattern as GBRG (2). However, testing showed that this resulted in solarization and weak colors (mostly black and white with a green/pink tint) under `hmirror=0, vflip=0`.
  - We added a C-level check in `mod_camera.c` that overrides the default sensor Bayer pattern to **BGGR (3)** when the sensor is identified as `"ov5647"`:
    ```c
    const char *sensor_name = esp_cam_sensor_get_name(camera_sensor);
    if (sensor_name && (strcmp(sensor_name, "ov5647") == 0 || strcmp(sensor_name, "OV5647") == 0)) {
        bayer_type = 3; // OV5647 default bayer pattern is BGGR (3) for hmirror=0, vflip=0
    }
    ```
  - This mathematically maps the hardware ISP `bayer_mode` register to `0` (BG/GR) for standard upright orientation, resolving the solarization and restoring correct, vibrant, full-color reproduction.
* **Fixed SCCB Register Access and CPU Hang**: 
  - Corrected the SCCB helper function names in `mod_camera.c` to `esp_sccb_transmit_receive_reg_a16v8` and `esp_sccb_transmit_reg_a16v8` to match the exact signatures in `esp_sccb_intf.h`.
  - Removed the blocking `while (REG_GET_BIT(ISP_CAM_CNTL_REG, ISP_CAM_UPDATE_REG));` check. Since the camera sensor clock is not yet running at the moment the ISP registers are configured, polling this bit caused the CPU to hang indefinitely on startup. Removing it allows the registers to safely commit automatically once the sensor stream starts.
* **Testing Results**: Successfully compiled, flashed, and verified live video streaming at **~33 FPS** with upright, bright, and vibrant color rendering on the Waveshare LCD screen.

---

## Waveshare P4 (4") Audio C-level Port & Bugfixes

We successfully transitioned the ES8311 audio codec initialization and volume control on the **Waveshare ESP32-P4 (4-inch)** board from Python to C:

### 1. Unified I2C Port Access (C-level Bindings)
* **Problem**: Probing the unpowered ES8311 codec on the shared I2C bus (SDA:7, SCL:8) clamped the data lines to ground, causing standard Python `machine.I2C` scans to hang the CPU. Additionally, initializing Python I2C on the same pins as the C touch driver led to I2C controller conflicts.
* **Solution**: Developed C-level I2C methods in `mod_waveshare_4.c` (and mirrored in `mod_waveshare.c` for consistency) using ESP-IDF's `i2c_master` API on the already-initialized `internal_i2c_bus` handle:
  * `waveshare.init_codec(volume_db)`: Configures power, unmutes the DAC, sets up standard clock dividers (MCLK-less mode), and initializes the ES8311.
  * `waveshare.set_codec_volume(volume_db)`: Directly writes to the codec's digital volume register.
  * `waveshare.check_codec()`: Safely reads the chip ID (`0xFD` / `0xFE`) to confirm presence on the shared bus.
* **Wrapper**: Rewrote [audio_setup.py](file:///g:/My%20Drive/ODD/Volu%20Sol/ESP32-P4%20MicroPython/audio_setup.py) to act as a thin Python wrapper calling the new C bindings.

### 2. Verification Results
* Successfully compiled, flashed, and verified on the Waveshare 4" hardware.
* **Touch & Display Coexistence**: The shared physical I2C bus operates concurrently for GT911 touch input and ES8311 register transactions without hangs.
* **Audio Playback**: The board successfully plays `gc.wav` files via `play_wav.py` with clean output.

---

## LVGL Re-initialization & Boot Settle Fixes

We resolved two critical runtime lockup issues when executing the `web_clock.py` application:

### 1. Robust LVGL Re-initialization (C-level)
* **Problem**: Calling `lv.init()` multiple times (e.g. at the start of `web_clock.main()`) invalidates LVGL's internal display and input device registers. However, the static `disp_handle` and `touch_indev` pointers in our C-level drivers remained non-NULL, causing the drivers to reuse stale/dangling handles. This led to silent CPU hangs during subsequnt `lv.timer_handler()` ticks.
* **Solution**: Updated `mod_waveshare_4.c`, `mod_waveshare.c`, and `mod_elecrow.c` to query the active LVGL display and input lists using `lv_display_get_next(NULL)` and `lv_indev_get_next(NULL)`. If these lists are empty (indicating a fresh `lv.init()` call), we reset `disp_handle` and `touch_indev` to `NULL` to trigger safe, clean re-creation and re-registration.

### 2. Startup Power & Co-processor Settle Delay Optimization (C-level & Python)
* **Problem**: The ESP32-C6 co-processor requires at least 1.5 to 2 seconds to initialize its internal SDIO slave stack after a hardware reset. Additionally, on physical button resets, the ESP32-P4's SDIO pins default to high-impedance/floating states. This causes the co-processor's strapping pins (specifically GPIO 9, which is shared with SDIO D3) to read wrong logic levels and boot into the ROM bootloader instead of flash. This led to a 10-15s SDIO card detection hang followed by reboots.
* **Solution**: 
  * **SDIO Strapping Override**: Configured SDIO pins **GPIO 14-19** as inputs with internal pull-up resistors enabled (`GPIO_PULLUP_ENABLE`) at the very start of the C boot sequence (`waveshare_startup()` / `elecrow_startup()`). This forces the co-processor's strapping pins HIGH during reset, guaranteeing it boots cleanly from flash on both physical button resets and power cycles.
  * **Wait Delay**: Configured the C boot sequence to wait a full **2.0 seconds** (`vTaskDelay(pdMS_TO_TICKS(2000))`) immediately after releasing the co-processor hardware reset to allow it to initialize its SDIO slave stack before the host starts scanning.
  * **Streamlined Python Sleeps**: Shifting the boot delay to C allowed us to safely reduce the Python sleeps in [main.py](file:///g:/My%20Drive/ODD/Volu%20Sol/ESP32-P4%20MicroPython/main.py) and [web_clock.py](file:///g:/My%20Drive/ODD/Volu%20Sol/ESP32-P4%20MicroPython/web_clock.py) to a brief **500ms** settle window for the display controller. The clock application now boots instantly without reboots.

### 3. Autostart Deployment
* Uploaded [main.py](file:///g:/My%20Drive/ODD/Volu%20Sol/ESP32-P4%20MicroPython/main.py) and [web_clock.py](file:///g:/My%20Drive/ODD/Volu%20Sol/ESP32-P4%20MicroPython/web_clock.py) to the board's root flash filesystem. The board now automatically boots directly into `web_clock.main()` after power-up or a hardware reset.




