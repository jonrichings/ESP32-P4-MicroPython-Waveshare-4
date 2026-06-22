import lvgl as lv
import time
import sys

# Import board driver
try:
    import waveshare as board
    board_name = "Waveshare P4 8\""
    width, height = 800, 1280
except ImportError:
    try:
        import elecrow as board
        board_name = "Elecrow P4 7\""
        width, height = 600, 1024
    except ImportError:
        print("Error: Waveshare or Elecrow board module not found.")
        sys.exit(1)

# Global variables for callback access
buttons = {}
cols, rows = 5, 8
cell_w = width // cols
cell_h = height // rows

def on_touch_event(e):
    code = e.get_code()
    if code in (lv.EVENT.PRESSING, lv.EVENT.PRESSED):
        point = lv.point_t()
        e.get_indev().get_point(point)
        tx, ty = point.x, point.y
        
        # Calculate grid coordinates
        grid_c = tx // cell_w
        grid_r = ty // cell_h
        
        # Check if touching the reset button cell (col 2, row 4)
        if grid_c == 2 and grid_r == 4:
            # Clear all grid buttons back to dark gray
            for (c, r), btn in buttons.items():
                if not (c == 2 and r == 4):
                    btn.set_style_bg_color(lv.color_hex(0x333333), 0)
        else:
            # Paint grid cell green
            if 0 <= grid_c < cols and 0 <= grid_r < rows:
                btn = buttons[(grid_c, grid_r)]
                btn.set_style_bg_color(lv.color_hex(0x4CAF50), 0) # Material green

def main():
    global buttons
    print(f"Initializing {board_name} Display & Touch Calibration Tool...")
    
    # 1. Initialize LVGL and Board display/touch hardware
    lv.init()
    board.init()
    
    # Force backlight ON
    board.set_backlight(1)
    
    # 2. Get active screen and style it
    scr = lv.screen_active()
    scr.set_style_bg_color(lv.color_hex(0x111111), 0) # Dark background
    scr.set_style_bg_opa(lv.OPA.COVER, 0)
    
    # Make screen background clickable to receive drag/touch events
    scr.add_flag(lv.obj.FLAG.CLICKABLE)
    scr.add_event_cb(on_touch_event, lv.EVENT.ALL, None)
    
    print(f"Screen configuration: {width}x{height}")
    print(f"Creating a {cols}x{rows} grid (cells are {cell_w}x{cell_h} pixels)...")
    
    # Create the grid of buttons
    for r in range(rows):
        for c in range(cols):
            is_reset_btn = (r == 4 and c == 2)
            
            btn = lv.button(scr)
            # 2 pixel padding between buttons to see grid lines
            btn.set_size(cell_w - 2, cell_h - 2)
            btn.set_pos(c * cell_w + 1, r * cell_h + 1)
            
            # Disable button clickability so touches fall through to screen background
            btn.remove_flag(lv.obj.FLAG.CLICKABLE)
            
            # Style the button
            if is_reset_btn:
                btn.set_style_bg_color(lv.color_hex(0xD81B60), 0) # Deep Pink/Red for Reset
            else:
                btn.set_style_bg_color(lv.color_hex(0x333333), 0) # Dark gray by default
                
            btn.set_style_bg_opa(lv.OPA.COVER, 0)
            btn.set_style_radius(8, 0) # slightly rounded corners
            
            # Label
            lbl = lv.label(btn)
            if is_reset_btn:
                lbl.set_text("RESET")
            else:
                lbl.set_text(f"{c},{r}")
            lbl.align(lv.ALIGN.CENTER, 0, 0)
            
            # Store in dict using tuple key
            buttons[(c, r)] = btn

    # Title label at the top
    title = lv.label(scr)
    title.set_text(f"{board_name} Touch Check")
    title.set_style_text_color(lv.color_hex(0x888888), 0)
    title.align(lv.ALIGN.TOP_MID, 0, 10)
    
    print("\n=======================================================")
    print(" TOUCH TEST ACTIVE: Drag your finger across the display")
    print(" - Untouched cells: Dark Gray")
    print(" - Reset cell (Middle): Red")
    print(" - Touched cells: turn green")
    print(" - Tap the RED 'RESET' cell in the center to clear.")
    print(" - Press Ctrl+C in terminal to stop.")
    print("=======================================================")
    
    try:
        while True:
            # Process LVGL tasks (this calls touchpad_read_cb inside C code)
            lv.timer_handler()
            time.sleep_ms(15)
            
    except KeyboardInterrupt:
        print("\nStopping touch test...")
    finally:
        # Perform soft deinit of board display
        board.deinit()
        print("Cleanup done.")

if __name__ == "__main__":
    main()

