import lvgl as lv
try:
    import waveshare as board
except ImportError:
    import elecrow as board
import time

# 1. Initialize
lv.init()
board.init()

# 2. Setup UI
scr = lv.screen_active()
btn = lv.button(scr)
btn.align(lv.ALIGN.CENTER, 0, 0)
label = lv.label(btn)
label.set_text("TAP ME!")

# 3. Touch Event Callback
def btn_event_cb(e):
    if e.get_code() == lv.EVENT.CLICKED:
        label.set_text("TOUCH OK!")
        print("Touch Verified!")

btn.add_event_cb(btn_event_cb, lv.EVENT.ALL, None)

print("Starting verification loop (10 seconds)...")
print("Please TAP THE BUTTON on the screen.")
for i in range(100):
    lv.timer_handler()
    time.sleep_ms(100)
print("Verification loop finished.")
