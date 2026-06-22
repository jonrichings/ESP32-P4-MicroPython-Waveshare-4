#ifndef MICROPY_HW_BOARD_NAME
#define MICROPY_HW_BOARD_NAME "WAVESHARE_P4_4"
#endif

#ifndef MICROPY_HW_MCU_NAME
#define MICROPY_HW_MCU_NAME "ESP32-P4"
#endif

// The ESP32-P4 has a native USB port, but we'll bypass TinyUSB for now. Use
// USB-Serial-JTAG or UART REPL.
#define MICROPY_HW_ENABLE_USBDEV (0)
#define MICROPY_HW_USB_CDC_NUM (0)

// Enable UART REPL explicitly since USB JTAG might disable it by default
#undef MICROPY_HW_ENABLE_UART_REPL
#define MICROPY_HW_ENABLE_UART_REPL (1)

// Define pure python modules directory if needed later
#define MICROPY_HW_MODULES_DIR "modules"

// I2C 0 (Internal - Audio Codec, Touch)
#define MICROPY_HW_I2C0_SCL (8)
#define MICROPY_HW_I2C0_SDA (7)

// Camera SCCB I2C (if needed later)
#define MICROPY_HW_I2C1_SCL (13)
#define MICROPY_HW_I2C1_SDA (12)

#define MICROPY_PY_NETWORK_HOSTNAME_DEFAULT "Waveshare-P4-4"

// The ESP32-P4 has no internal Wi-Fi/Bluetooth MAC.
// Disable these modules so their components aren't linked.
#undef MICROPY_PY_NETWORK_WLAN
#define MICROPY_PY_NETWORK_WLAN (1)

#undef MICROPY_PY_NETWORK_ESP_HOSTED
#define MICROPY_PY_NETWORK_ESP_HOSTED (1)

#undef MICROPY_PY_BLUETOOTH
#define MICROPY_PY_BLUETOOTH (1)

#undef MICROPY_BLUETOOTH_NIMBLE
#define MICROPY_BLUETOOTH_NIMBLE (1)

#undef MICROPY_BLUETOOTH_NIMBLE_BINDINGS_ONLY
#define MICROPY_BLUETOOTH_NIMBLE_BINDINGS_ONLY (1)

#undef MICROPY_PY_NETWORK
#define MICROPY_PY_NETWORK (1)
#undef MICROPY_PY_ESPNOW
#define MICROPY_PY_ESPNOW (0)

#define MICROPY_HW_ENABLE_MDNS_QUERIES (1)
#define MICROPY_HW_ENABLE_MDNS_RESPONDER (1)

// Increase heap and stack for the powerful ESP32-P4 with 32MB PSRAM
#define MICROPY_GC_INITIAL_HEAP_SIZE (16 * 1024 * 1024)
#define MICROPY_TASK_STACK_SIZE (64 * 1024)

#undef MICROPY_PY_NETWORK
#undef MICROPY_PY_NETWORK
#define MICROPY_PY_NETWORK (1)

void waveshare_startup(void);
#define MICROPY_BOARD_STARTUP waveshare_startup
void waveshare_deinit(void);
#define MICROPY_BOARD_DEINIT waveshare_deinit
