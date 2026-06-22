# main.py - Autonomous entry point for Elecrow P4 Clock
import gc
import time

# Give the system and terminal a moment to settle after power-up
time.sleep(2)

print("Starting Web Clock...")
import web_clock

try:
    web_clock.main()
except Exception as e:
    print("Main App Error:", e)
    # Optional: machine.reset() on critical failure? 
    # For now, just drop to REPL for debugging.
