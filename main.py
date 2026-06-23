# main.py - Autonomous entry point for Elecrow P4 Clock
import gc
import time

# Give the terminal link a brief moment to stabilize
time.sleep_ms(500)

print("Starting Web Clock...")
import web_clock

try:
    web_clock.main()
except Exception as e:
    print("Main App Error:", e)
    # Optional: machine.reset() on critical failure? 
    # For now, just drop to REPL for debugging.
