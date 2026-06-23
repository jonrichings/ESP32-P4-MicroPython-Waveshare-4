import lvgl as lv
import os
import waveshare as board
board_name = "Waveshare P4 4\""
import network
import time
import ntp
import machine
import bluetooth

import struct
import gc

# --- Configuration ---
WIFI_SSID = "MADELEINE"
WIFI_PASS = "niftysun167"
TIMEZONE_OFFSET_HOURS = -7
NTP_SYNC_INTERVAL = 600

# --- Globals ---
ui_clock_label = None
ui_date_label = None
ui_bt_label = None
ui_boot_label = None
ui_status_label = None
ui_location_label = None
last_ntp_sync = 0
main_scr = None

def format_time(tm):
    return "{:02d}:{:02d}:{:02d}".format(tm[3], tm[4], tm[5])

def format_date(tm):
    days = ["Mon","Tue","Wed","Thu","Fri","Sat","Sun"]
    months = ["Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"]
    # tm: (y, m, d, h, m, s, weekday, yearday)
    return "{} {} {:02d}, {}".format(days[tm[6]], months[tm[1]-1], tm[2], tm[0])

def handle_manual_time_adj(point):
    global last_ntp_sync
    tm = list(time.localtime())
    if point.x < 512:
        tm[3] = (tm[3] + 1) % 24
        msg = "Adjust: Hour -> {:02d}".format(tm[3])
    else:
        tm[4] = (tm[4] + 1) % 60
        tm[5] = 0 # Reset seconds
        msg = "Adjust: Min -> {:02d}".format(tm[4])
    
    print(msg)
    show_status(msg)
    
    # Update RTC: (y, m, d, weekday, h, m, s, sub)
    machine.RTC().datetime((tm[0], tm[1], tm[2], tm[6], tm[3], tm[4], tm[5], 0))
    
    # Postpone next sync
    last_ntp_sync = time.time() + 1800
    
    # Update UI immediately
    if ui_clock_label:
        ui_clock_label.set_text(format_time(tm))
    if ui_date_label:
        ui_date_label.set_text(format_date(tm))

def show_status(text, duration=3):
    global ui_status_label
    if ui_status_label:
        ui_status_label.set_text(text)
        # Clear after duration
        def clear_status(t):
            if ui_status_label: ui_status_label.set_text("")
        tmr = lv.timer_create(clear_status, duration * 1000, None)
        tmr.set_repeat_count(1)

def on_screen_click(e):
    point = lv.point_t()
    e.get_indev().get_point(point)
    handle_manual_time_adj(point)

def trigger_ntp_sync(e=None):
    show_status("Triggering NTP Sync...")
    # No reset during runtime
    sync_ntp_safe(display_active=True, aggressive=True, allow_reset=False)

def show_boot_message(text):
    global ui_boot_label
    print("BOOT:", text)
    try:
        # If we have a main screen, use the status label instead
        if ui_status_label:
            ui_status_label.set_text(text)
            lv.timer_handler()
            return

        if ui_boot_label is None:
            scr = lv.screen_active()
            ui_boot_label = lv.label(scr)
            # Ensure background is BLACK
            scr.set_style_bg_color(lv.color_hex(0x000000), 0)
            scr.set_style_bg_opa(255, 0)
            
            font = lv.font_montserrat_28 if hasattr(lv, 'font_montserrat_28') else lv.font_montserrat_14
            ui_boot_label.set_style_text_font(font, 0)
            ui_boot_label.set_style_text_align(lv.TEXT_ALIGN.CENTER, 0)
            ui_boot_label.set_width(scr.get_width())
            ui_boot_label.set_style_text_color(lv.color_hex(0xFFFFFF), 0)
            ui_boot_label.align(lv.ALIGN.CENTER, 0, 0)
        
        ui_boot_label.set_text(text)
        lv.timer_handler()
    except: pass

def reset_wifi_slave():
    print("Stabilizing WiFi Slave (GPIO 32)...")
    try:
        p = machine.Pin(32, machine.Pin.OUT)
        p.value(1) # Reset ON
        time.sleep_ms(100)
        p.value(0) # Reset OFF (Run)
        time.sleep(3) # Wait for co-processor to boot
        return True
    except: return False

def init_wifi(allow_reset=False):
    gc.collect()
    wlan = network.WLAN(network.STA_IF)
    for retry in range(2):
        try:
            if not wlan.active():
                wlan.active(True)
            if wlan.isconnected(): return True
            
            show_boot_message("Connecting WiFi...\n({})".format(WIFI_SSID))
            wlan.connect(WIFI_SSID, WIFI_PASS)
            for _ in range(15):
                if wlan.isconnected(): return True
                time.sleep(1)
                print(".", end="")
            print()
        except Exception as e:
            print("WiFi Err:", e)
            # ONLY perform a hardware reset if we are in the initial boot phase.
            # Toggling GPIO 32 while the display is active can cause a system reboot.
            if allow_reset:
                reset_wifi_slave()
            else:
                break # Just fail gracefully instead of crashing the P4
    return wlan.isconnected()

def sync_ntp_safe(display_active=False, aggressive=False, allow_reset=False):
    global last_ntp_sync
    if not init_wifi(allow_reset=allow_reset): 
        msg = "WiFi Unavailable"
        if display_active: show_status(msg)
        print(msg)
        return False
    
    msg = "Syncing Time..."
    if display_active: show_boot_message(msg)
    print(msg)
    
    try:
        retries = 3 if aggressive else 1
        if ntp.set_time(TIMEZONE_OFFSET_HOURS, aggressive_retries=retries):
            last_ntp_sync = time.time()
            show_status("Sync OK")
            if ui_location_label:
                ui_location_label.set_text(ntp.get_timezone_name())
            return True
    except Exception as e:
        print("Sync Error:", e)
    show_status("Sync Failed")
    return False

def create_ui():
    global ui_clock_label, ui_date_label, ui_bt_label, ui_boot_label, ui_status_label, main_scr
    
    print("Creating Main UI...")
    
    # Create a fresh screen object
    main_scr = lv.obj()
    main_scr.set_style_bg_color(lv.color_hex(0x000000), 0)
    main_scr.set_style_bg_opa(255, 0)
    
    # Hide boot text immediately by clearing screen active if needed
    # (Actually screen_load will handle it)
    
    # Set the whole screen to be clickable for time adjustment
    main_scr.add_flag(lv.obj.FLAG.CLICKABLE)
    main_scr.add_event_cb(on_screen_click, lv.EVENT.CLICKED, None)
    
    font_large = lv.font_montserrat_48 if hasattr(lv, 'font_montserrat_48') else lv.font_montserrat_14
    font_med = lv.font_montserrat_28 if hasattr(lv, 'font_montserrat_28') else lv.font_montserrat_14
    font_small = lv.font_montserrat_14

    # Clock Label
    ui_clock_label = lv.label(main_scr)
    ui_clock_label.set_text("--:--:--")
    ui_clock_label.set_style_text_font(font_large, 0)
    ui_clock_label.set_style_text_color(lv.color_hex(0xFFFFFF), 0)
    ui_clock_label.align(lv.ALIGN.CENTER, 0, -40)

    # Date Label
    ui_date_label = lv.label(main_scr)
    ui_date_label.set_text("UTC Sync...")
    ui_date_label.set_style_text_font(font_med, 0)
    ui_date_label.set_style_text_color(lv.color_hex(0xAAAAAA), 0)
    ui_date_label.align(lv.ALIGN.CENTER, 0, 60)
    # Tapping date triggers NTP
    ui_date_label.add_flag(lv.obj.FLAG.CLICKABLE)
    ui_date_label.add_event_cb(trigger_ntp_sync, lv.EVENT.CLICKED, None)

    # Status/Boot Label (Reusable)
    ui_status_label = lv.label(main_scr)
    ui_status_label.set_text("")
    ui_status_label.set_style_text_font(font_small, 0)
    ui_status_label.set_style_text_color(lv.color_hex(0x00FF00), 0)
    ui_status_label.align(lv.ALIGN.BOTTOM_MID, 0, -60)

    # Location/Timezone Label
    ui_location_label = lv.label(main_scr)
    ui_location_label.set_text(ntp.get_timezone_name())
    ui_location_label.set_style_text_font(font_small, 0)
    ui_location_label.set_style_text_color(lv.color_hex(0x666666), 0)
    ui_location_label.align(lv.ALIGN.BOTTOM_MID, 0, -45)

    # Info
    info = lv.label(main_scr)
    info.set_text("{} Clock v0.9.0".format(board_name))
    info.set_style_text_color(lv.color_hex(0x444444), 0)
    info.align(lv.ALIGN.BOTTOM_MID, 0, -20)
    
    # Load the new screen
    lv.screen_load(main_scr)
    lv.timer_handler()
    
    # Cleanup boot label
    if ui_boot_label:
        ui_boot_label.delete()
        ui_boot_label = None

def main():
    global last_ntp_sync
    print("--- Phase 0: Hardware Init ---")
    lv.init()
    
    # Initialize Display Early (User wants to see boot notices)
    board.init()
    
    # Set background to black immediately
    scr = lv.screen_active()
    scr.set_style_bg_color(lv.color_hex(0x000000), 0)
    scr.set_style_bg_opa(255, 0)
    
    board.set_backlight(1)
    
    # Give the DSI link a moment to settle
    time.sleep_ms(500)
    
    print("--- Phase 1: Sync (With Notices) ---")
    # ONLY ALLOW Hardware Reset during initial boot phase
    sync_ntp_safe(display_active=True, aggressive=True, allow_reset=True)
    
    # Transition to main UI
    create_ui()
    
    print("Clock Active.")
    last_sec = -1
    
    while True:
        try:
            tm = time.localtime()
            now = time.time()
            
            if tm[5] != last_sec:
                last_sec = tm[5]
                if ui_clock_label:
                    ui_clock_label.set_text(format_time(tm))
                if ui_date_label:
                    ui_date_label.set_text(format_date(tm))
                
                if now - last_ntp_sync > NTP_SYNC_INTERVAL:
                    sync_ntp_safe()
                    
            lv.timer_handler()
            time.sleep_ms(10)
            
            if tm[5] == 0: gc.collect()
            
        except Exception as e:
            print("Loop Err:", e)
            time.sleep(1)

if __name__ == "__main__":
    main()