import machine
import time
import bluetooth
import struct

# Active-high network slave reset pin (GPIO 32)
RESET_PIN = 32

def init_coprocessor():
    print("\n[1/2] Checking/Initializing C6 co-processor (GPIO 32)...")
    try:
        rst = machine.Pin(RESET_PIN, machine.Pin.OUT)
        # Pulse reset pin (1 = reset, 0 = run)
        rst.value(1)
        time.sleep_ms(100)
        rst.value(0)
        time.sleep(2)  # Give C6 firmware ample time to boot up
        print("Co-processor boot initialized successfully.")
    except Exception as e:
        print("Error initializing co-processor reset pin:", e)

def build_advertising_payload(name):
    payload = bytearray()
    
    def append_field(ad_type, data):
        nonlocal payload
        payload.append(len(data) + 1)
        payload.append(ad_type)
        payload.extend(data)
        
    # Flags field: General Discoverable Mode, BR/EDR Not Supported
    append_field(0x01, struct.pack("<B", 0x06))
    
    # Complete Local Name
    if name:
        append_field(0x09, name.encode("utf-8"))
        
    return payload

def run_advertisement(ble, dev_name="Waveshare P4_BLE"):
    print(f"\n[2/2] Building advertising payload for name: '{dev_name}'")
    adv_payload = build_advertising_payload(dev_name)
    
    print(f"Starting BLE advertisement...")
    # interval_us = 500ms (500000us)
    ble.gap_advertise(500000, adv_data=adv_payload)
    
    print("\n==================================================")
    print(f" BLE ADVERTISING ACTIVE: '{dev_name}' ")
    print(" Scan on your phone using a BLE app (e.g. nRF Connect).")
    print(" Press Ctrl+C in terminal/REPL to stop advertising.")
    print("==================================================")
    
    try:
        while True:
            print(".", end="")
            time.sleep(2)
    except KeyboardInterrupt:
        print("\nStopping BLE advertisement...")
    finally:
        ble.gap_advertise(None)
        print("BLE advertising stopped.")

def main():
    # Make sure co-processor is powered and running
    init_coprocessor()
    
    print("Initializing BLE driver...")
    ble = bluetooth.BLE()
    ble.active(True)
    
    try:
        # Step 1: Advertise
        run_advertisement(ble, dev_name="Waveshare P4_BLE")
    finally:
        # Cleanup
        print("Deactivating BLE...")
        ble.active(False)
        print("Test complete.")

if __name__ == "__main__":
    main()

