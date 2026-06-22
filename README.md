# Elecrow-P4-Display-MPY

A functional MicroPython toolkit for the Elecrow ESP32-P4 Advanced Phone (7-inch).

## Overview
This repository contains the optimized firmware and Python applications for the ESP32-P4 Rev 0. It includes fixes for display stability, WiFi/Bluetooth coexistence, and core system crashes.

### Key Features
- **Web Clock**: A smooth digital clock with NTP sync and BLE advertising.
- **Audio Support**: Working I2S/LDO configuration for the onboard amp.
- **STC8 Integration**: Native commands for backlight and power management.

## Getting Started
1. **Flash Firmware**: Use the provided `idf.py` commands in the [walkthrough](p4_stabilization_guide.md) to build and flash the stabilized MicroPython core.
2. **Run Apps**: 
   ```bash
   mpremote run web_clock.py
   ```

## Documentation
- [P4 Stabilization Guide](p4_stabilization_guide.md): Critical hardware workarounds for the Revision 0 silicon.
- [Walkthrough](walkthrough.md): Deployment and verification steps.
