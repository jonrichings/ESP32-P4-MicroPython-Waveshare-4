import lvgl as lv
import elecrow
lv.init()
elecrow.init()
print('elecrow.init() complete')
lv.timer_handler()
print('lv.timer_handler() complete')
scr = lv.screen_active()
print('lv.screen_active() returned', scr)
print('Done')
