import lvgl as lv
import time
import camera
import sys
import uctypes

# 1. Detect and Initialize HMI Board
try:
    import waveshare as board
    board_name = "Waveshare P4 8\""
    sda_pin, scl_pin = 7, 8
    cam_w, cam_h = 800, 640
except ImportError:
    try:
        import elecrow as board
        board_name = "Elecrow P4 7\""
        sda_pin, scl_pin = 12, 13
        cam_w, cam_h = 800, 640
    except ImportError:
        print("Error: Board driver (waveshare or elecrow) not found.")
        sys.exit(1)

print(f"Initializing {board_name} Display & Backlight...")
lv.init()
board.init()
board.set_backlight(1)

# 2. Initialize Camera Module
print(f"Initializing MIPI CSI camera on SDA:{sda_pin}, SCL:{scl_pin} at {cam_w}x{cam_h}...")
try:
    actual_w, actual_h = camera.init(sda=sda_pin, scl=scl_pin, hres=cam_w, vres=cam_h, hmirror=0, vflip=0, exposure=120)
    print(f"Camera initialized successfully. Actual resolution: {actual_w}x{actual_h}")
except Exception as e:
    print("Camera initialization failed:", e)
    board.deinit()
    sys.exit(1)

# 3. Pre-allocate aligned buffer in PSRAM for frame capture (64-byte alignment)
buf_size = actual_w * actual_h * 2
raw_buf = bytearray(buf_size + 64)
addr = uctypes.addressof(raw_buf)
offset = (64 - (addr % 64)) % 64
buf = memoryview(raw_buf)[offset : offset + buf_size]
print(f"Pre-allocated aligned frame buffer size={buf_size} at address {hex(addr + offset)}")

# 4. Configure LVGL Canvas for live preview
scr = lv.screen_active()
scr.set_style_bg_color(lv.color_hex(0x000000), 0)

canvas = lv.canvas(scr)

# Detect LVGL 9 color format enum
color_format = None
# Try to find it in lv.COLOR_FORMAT
if hasattr(lv, "COLOR_FORMAT"):
    for attr in ["RGB565", "RGB565D", "RGB565_SWAP"]:
        if hasattr(lv.COLOR_FORMAT, attr):
            color_format = getattr(lv.COLOR_FORMAT, attr)
            print(f"LVGL 9 Color Format found: lv.COLOR_FORMAT.{attr} ({color_format})")
            break
if color_format is None:
    # Try direct attributes
    for attr in ["COLOR_FORMAT_RGB565", "COLOR_FORMAT_RGB565_SWAP"]:
        if hasattr(lv, attr):
            color_format = getattr(lv, attr)
            print(f"LVGL Color Format found: lv.{attr} ({color_format})")
            break
if color_format is None:
    # Fallback to standard integer format ID for RGB565 in LVGL 9 (which is 16)
    color_format = 16
    print(f"LVGL Color Format fallback to: {color_format}")

canvas.set_buffer(buf, actual_w, actual_h, color_format)
canvas.align(lv.ALIGN.CENTER, 0, 0)

# Add a text label showing FPS and instructions
label = lv.label(scr)
label.set_text("Live Camera Preview - Press Ctrl+C in terminal to stop")
label.set_style_text_color(lv.color_hex(0xFFFFFF), 0)
label.align(lv.ALIGN.BOTTOM_MID, 0, -20)

print("\n=======================================================")
print(" CAMERA PREVIEW ACTIVE: Live stream rendering on LCD screen")
print(" - Camera resolution: 800x640 (RGB565)")
print(" - Press Ctrl+C in your terminal to stop the preview.")
print("=======================================================\n")

# 5. Capture loop
frame_count = 0
start_time = time.time()

try:
    while True:
        # Capture frame directly into the bytearray buffer
        bytes_received = camera.capture(buf)
        
        # Invalidate the canvas to force LVGL to redraw the new pixels from the buffer
        canvas.invalidate()
        
        # Process pending LVGL display updates
        lv.timer_handler()
        
        frame_count += 1
        if frame_count % 30 == 0:
            elapsed = time.time() - start_time
            if elapsed > 0:
                fps = frame_count / elapsed
                label.set_text(f"Live Camera Preview ({fps:.1f} FPS)")
                print(f"Rendered {frame_count} frames, current speed: {fps:.1f} FPS")
                
        # Small delay to keep execution friendly
        time.sleep_ms(5)

except KeyboardInterrupt:
    print("\nStopping camera preview...")

finally:
    # Clean up handles
    camera.deinit()
    board.deinit()
    print("Camera and display de-initialized. Test finished.")
