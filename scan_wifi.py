import network, machine, time

# Active-high WiFi slave reset pin (GPIO 32). 
# 1 = Reset ON, 0 = Run (Normal operation)
RESET_PIN = 32

def scan():
    print("Initializing WLAN interface...")
    
    # Ensure WiFi slave is active (C firmware already reset it at boot)
    try:
        rst = machine.Pin(RESET_PIN, machine.Pin.OUT)
        rst.value(0) # Reset OFF (Run)
        time.sleep_ms(500) # Wait for stability
    except Exception as e:
        print("Failed to configure reset pin:", e)

    wlan = network.WLAN(network.STA_IF)
    try:
        wlan.active(True)
        print("WLAN active. Scanning for networks...")
        
        # Scan returns list of tuples: (ssid, bssid, channel, RSSI, authmode, hidden)
        networks = wlan.scan()
        print("\nScan complete. Found {} networks:".format(len(networks)))
        print("-" * 50)
        for net in networks:
            ssid = net[0].decode('utf-8', 'ignore')
            rssi = net[3]
            channel = net[2]
            print(f"SSID: {ssid:<25} | RSSI: {rssi:>4} dBm | Channel: {channel}")
        print("-" * 50)
    except Exception as e:
        print("Scan failed:", e)
    finally:
        wlan.active(False)

if __name__ == "__main__":
    scan()
