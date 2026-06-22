import network, machine, time, ntp

# Active-high WiFi slave reset pin (GPIO 32). 
RESET_PIN = 32
WIFI_SSID = "MADELEINE"
WIFI_PASS = "niftysun167"
TIMEZONE_OFFSET = -7

def connect():
    print("Ensuring WiFi slave is active...")
    try:
        rst = machine.Pin(RESET_PIN, machine.Pin.OUT)
        # Note: Do NOT hold in reset. Set to 0.
        rst.value(0) 
        time.sleep_ms(100)
    except Exception as e:
        print("Reset Pin Config Err:", e)

    wlan = network.WLAN(network.STA_IF)
    if not wlan.active():
        wlan.active(True)
        
    if wlan.isconnected():
        print("Already connected! IP:", wlan.ifconfig()[0])
    else:
        print(f"Connecting to SSID: '{WIFI_SSID}'...")
        wlan.connect(WIFI_SSID, WIFI_PASS)
        
        # Wait up to 15 seconds for connection
        for i in range(15):
            if wlan.isconnected():
                break
            time.sleep(1)
            print(".", end="")
        print()
        
    if wlan.isconnected():
        print("Connected successfully! IP info:", wlan.ifconfig())
        print("Triggering NTP time synchronization...")
        try:
            # Sync time via geolocation and NTP
            if ntp.set_time(TIMEZONE_OFFSET, aggressive_retries=3):
                print("Time sync successful!")
                print("Local time on RTC:", time.localtime())
            else:
                print("Time sync failed.")
        except Exception as e:
            print("NTP sync error:", e)
    else:
        print("Connection failed. Status code:", wlan.status())
        # Print status details
        status = wlan.status()
        if status == network.STAT_CONNECTING:
            print("Status: Connecting...")
        elif status == network.STAT_WRONG_PASSWORD:
            print("Status: Wrong Password!")
        elif status == network.STAT_NO_AP_FOUND:
            print("Status: AP not found!")
        elif status == network.STAT_CONNECT_FAIL:
            print("Status: Connection failed!")
            
if __name__ == "__main__":
    connect()
