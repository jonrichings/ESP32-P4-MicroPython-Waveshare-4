import lvgl as lv
try:
    import waveshare as board
except ImportError:
    import elecrow as board
import time

lv.init()
board.init()

print("Touch Diagnostic Active.")
print("Tap the screen to see coordinates...")

while True:
    t = board.read_touch()
    if t:
        print("Raw Touch:", t)
    time.sleep_ms(50)
