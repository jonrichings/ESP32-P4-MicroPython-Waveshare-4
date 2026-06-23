# Waveshare-P4-Display-MPY (4-Inch Edition)

BE AWARE THAT I AM NOT A GREAT CODER.  THESE MICROPYTHON .BIN'S WERE THE RESULT OF A LOT OF TRIAL AND ERROR AND GEMINI SHINING A PENCIL BEAM ON TRICKY BITS.  DO NOT ASSUME THIS IS A PLUG AND PLAY REPO.  IT WORKS FOR ME BUT MAY ONLY BE A STARTING POINT FOR YOU.

A dedicated, stabilized MicroPython toolkit and firmware configuration for the **Waveshare ESP32-P4 4-inch** board (the little square one in a wall mount box shape.

## Project Purpose
This repository is specialized for the Waveshare 4-inch MIPI display board. It is decoupled from the other variants to allow direct modifications without introducing regressions.

## Firmware Binary
The pre-compiled stabilized firmware is located in:
- [firmware/micropython.bin](firmware/micropython.bin)

## How to Flash
To flash the firmware, run the following command in your activated ESP-IDF shell:
```bash
idf.py -p <PORT> flash
```
Or use `esptool.py` to write the individual components (e.g. if you build locally on `C:\esp-build\ESP32-P4_MicroPython_Waveshare-4_src`):
```bash
esptool.py --chip esp32p4 -p <PORT> -b 1152000 write_flash 0x2000 bootloader.bin 0x8000 partition-table.bin 0x10000 micropython.bin
```

---

## Subcomponent Support Status

| Subcomponent | Status | Pinout / Details |
| :--- | :--- | :--- |
| **Display** | **Done & Tested** | 4-inch MIPI DSI display, stable without blackouts. |
| **Touch** | **Done & Tested** | GT911 capacitive touch controller on shared I2C bus (pins 7/8). |
| **SD Card** | **Done & Tested** | Co-exists with ESP-Hosted WiFi using C-level SDMMC host sharing patch (slot=0). |
| **WiFi / BLE** | **Done & Tested** | Concurrent WiFi and BLE NimBLE. Stabilized by reducing SDIO bus clock to 20MHz and toggling GPIO 32 hardware reset at boot. |
| **Audio** | **Done & Tested** | Onboard ES8311 Audio Codec (I2C address 0x18, SDA:7, SCL:8). Native C initialization, volume registers, and speaker enable (GPIO 53, active high). Pins: SCK=12, WS=10, SD=9. |
| **Camera** | **Done & Tested** | OV5647 camera sensor configuration (SDA 7, SCL 8, address 0x36). |
| **Backlight** | **Done & Tested** | Backlight control via `waveshare` module. |

---

## Running Applications
Upload your files (e.g., `main.py`, `web_clock.py`) using `mpremote`:
```bash
mpremote fs cp main.py :main.py
mpremote fs cp web_clock.py :web_clock.py
mpremote reset
```
