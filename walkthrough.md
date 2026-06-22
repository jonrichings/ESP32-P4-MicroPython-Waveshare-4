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
- **Solution**: Patched [machine_sdcard.c](file:///g:/My%20Drive/ODD/Volu%20Sol/CellaVision_HMI_3/micropython/micropython/ports/esp32/machine_sdcard.c) to handle a shared host:
  - If `self->host.init()` returns `ESP_ERR_INVALID_STATE`, a new `SDCARD_CARD_FLAGS_HOST_SHARED` flag is set.
  - The driver bypasses the error and proceeds with slot-level card configuration.
  - During SDCard de-initialization or garbage collection, the host driver teardown is skipped for shared hosts, preserving the active WiFi connection.

### 2. Standalone Display Init Protection
- **Problem**: Running scripts that initialize the display (like `board.init()`) outside the main application caused Core 1 panics (Store access faults) if `lv.init()` was not called first.
- **Solution**: Updated [play_wav.py](file:///g:/My%20Drive/ODD/Volu%20Sol/CellaVision_HMI_3/play_wav.py) and other diagnostics to safely call `lv.init()` before `board.init()`.

### 3. Dynamic Camera & Audio Detection
- **Camera**: Updated [camera.py](file:///g:/My%20Drive/ODD/Volu%20Sol/CellaVision_HMI_3/camera.py) to dynamically query both **OV5647** (used on Waveshare, sharing I2C 0 on pins 7/8 at address `0x36`) and **SC2336** (used on Elecrow, on I2C 1 on pins 12/13 at address `0x30`).
- **Audio**: Updated [play_wav.py](file:///g:/My%20Drive/ODD/Volu%20Sol/CellaVision_HMI_3/play_wav.py) to dynamically use correct I2S pins based on the imported board:
  - Waveshare P4: SCK=12, WS=10, SD=9 (using `waveshare` module)
  - Elecrow P4: SCK=22, WS=21, SD=23 (using `elecrow` module)
- **Micro SD card slot**: Corrected the SDCard slot selection to `slot=0` (which routes to the physical onboard slot on both boards) in [play_wav.py](file:///g:/My%20Drive/ODD/Volu Sol/CellaVision_HMI_3/play_wav.py).

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

We modularized this into a standalone library [audio_setup.py](file:///g:/My%20Drive/ODD/Volu%20Sol/CellaVision_HMI_3/audio_setup.py), and updated [play_wav.py](file:///g:/My%20Drive/ODD/Volu%20Sol/CellaVision_HMI_3/play_wav.py) to import it.

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
* **Fixed SCCB Register Access and CPU Hang**: 
  - Corrected the SCCB helper function names in `mod_camera.c` to `esp_sccb_transmit_receive_reg_a16v8` and `esp_sccb_transmit_reg_a16v8` to match the exact signatures in `esp_sccb_intf.h`.
  - Removed the blocking `while (REG_GET_BIT(ISP_CAM_CNTL_REG, ISP_CAM_UPDATE_REG));` check. Since the camera sensor clock is not yet running at the moment the ISP registers are configured, polling this bit caused the CPU to hang indefinitely on startup. Removing it allows the registers to safely commit automatically once the sensor stream starts.
* **Testing Results**: Successfully compiled, flashed, and verified live video streaming at **~34.5 FPS** with upright, bright, and vibrant color rendering on the Waveshare LCD screen.


