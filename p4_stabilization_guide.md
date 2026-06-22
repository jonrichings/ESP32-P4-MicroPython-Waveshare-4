# ESP32-P4 Hardware Stabilization (Rev 0 / ECO2)

This document summarizes the critical findings and workarounds required to run a stable MicroPython environment with concurrent WiFi, Bluetooth, and MIPI DSI Display activity on the ESP32-P4 Revision 0 hardware (Elecrow P4 Advanced Phone).

## Key Learnings

### 1. SDIO Bus Stability (WiFi/Bluetooth Bridge)
- **Problem**: The `esp-hosted` bridge (SDIO interface) is prone to desynchronization and "Unknown Error 0xffffffff" during high Radio/Display concurrency.
- **Root Cause**: Bus congestion and tight timing margins on the Rev 0 silicon.
- **Solution**: 
    - Reduce SDIO clock from 40MHz to **20MHz** (`CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ`).
    - Implement a **Hardware Reset** for the slave chip (GPIO 32, Active High) in the boot sequence. This ensures a clean state and handles recovery after bridge crashes.
    - Set `CONFIG_ESP_HOSTED_SDIO_RESET_DELAY_MS` to at least **2000ms** to allow the slave chip (ESP32-C6) to initialize.

### 2. Software Stability (NimBLE Bluetooth)
- **Problem**: "Core 1 panic'ed (Load access fault)" when Bluetooth is active.
- **Root Cause**: The MicroPython build system's scanner misses the `MP_REGISTER_ROOT_POINTER` macro if it's in a peripheral file (`mpnimbleport.c`) that isn't always scanned early enough.
- **Solution**: Move the `bluetooth_nimble_root_pointers` registration to `main.c`. This ensures the root pointer is always present in the VM state, preventing background tasks from dereferencing NULL.

### 3. Display Flickering
- **Workaround**: Previous logic suspended the display for 10s during NTP sync to avoid PSRAM/DMA conflicts.
- **Refinement**: Now that the core stability is fixed, small network bursts (NTP, news snippets) do **not** require display suspension. The screen can stay on throughout.

## Future Revisions (Rev 3+ / ECO3)

The ESP32-P4 Revision 3 (ECO3) is expected to fix several internal bus arbitration issues.

### What to change for Rev 3+:
1.  **SDIO Clock**: You can likely revert to **40MHz** for higher throughput.
2.  **DMA Safety**: Aggressive interrupt pinning (`CONFIG_LCD_DSI_ISR_IRAM_SAFE`) might be less critical.
3.  **Concurrency**: The PSRAM/DMA bus "choking" seen on Rev 0 should be mitigated, allowing even higher refresh rates during radio activity.

---
*Note: Always check `MICROPY_BOARD_REV` during build to apply these conditionally if needed.*
