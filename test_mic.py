import machine
import time
import struct
import math
from machine import I2S, Pin

# Main I2C address for ES7210 ADC chip
ES7210_ADDR = 0x40

# Pins for Waveshare ESP32-P4
I2C_SDA = 7
I2C_SCL = 8

I2S_SCK = 12
I2S_WS = 10
I2S_SD = 11  # Data input (DIN) from ES7210 microphone

def init_es7210(i2c):
    print("[1/3] Initializing ES7210 Microphone ADC over I2C...")
    def write_reg(reg, val):
        i2c.writeto_mem(ES7210_ADDR, reg, bytes([val]))

    try:
        # Software Reset
        write_reg(0x00, 0xFF)
        time.sleep_ms(50)
        
        # Clock / System Power configuration
        write_reg(0x01, 0x3A)
        write_reg(0x02, 0x00)
        write_reg(0x03, 0x20)
        
        # Enable bias and ADC channels (Mic 1 & Mic 2)
        write_reg(0x04, 0x01)
        
        # ADC Digital interface settings (16-bit, I2S mode, slot configuration)
        write_reg(0x40, 0x42)
        write_reg(0x41, 0x70)
        write_reg(0x42, 0x10)
        write_reg(0x43, 0x10)
        
        # Set digital gain of ADC channels to 0dB (no attenuation)
        write_reg(0x44, 0x00)
        write_reg(0x45, 0x00)
        write_reg(0x46, 0x00)
        
        print("ES7210 Codec initialized successfully.")
        return True
    except Exception as e:
        print("ES7210 Codec initialization failed:", e)
        return False

def main():
    print("Waveshare ESP32-P4 Microphone Test")
    print("==================================")
    
    # 1. Initialize display singleton (LVGL) to prevent conflict panics
    # (Walkthrough: LVGL init is required before hardware init on this build)
    try:
        import lvgl as lv
        lv.init()
    except Exception as e:
        print("LVGL init failed/skipped (might be already initialized):", e)
        
    try:
        import waveshare as board
        board.init()
    except Exception as e:
        print("Waveshare board init failed/skipped:", e)

    # 2. Setup I2C & ES7210
    i2c = machine.I2C(0, sda=Pin(I2C_SDA), scl=Pin(I2C_SCL), freq=100000)
    if not init_es7210(i2c):
        print("Aborting test due to codec initialization failure.")
        return

    # 3. Setup I2S input channel
    print("[2/3] Initializing I2S RX interface (16kHz, 16-bit, Stereo)...")
    audio_in = I2S(0, 
                   sck=Pin(I2S_SCK), 
                   ws=Pin(I2S_WS), 
                   sd=Pin(I2S_SD), 
                   mode=I2S.RX, 
                   bits=16, 
                   format=I2S.STEREO, 
                   rate=16000, 
                   ibuf=4096)

    print("[3/3] Audio input active! Speak, clap, or make noise near the board.")
    print("Press Ctrl+C in your terminal to stop the test.\n")

    # Read buffers and compute RMS amplitude
    buf = bytearray(1024)
    try:
        while True:
            n_bytes = audio_in.readinto(buf)
            if n_bytes > 0:
                # 16-bit signed PCM samples
                samples = struct.unpack('<%dh' % (n_bytes // 2), buf[:n_bytes])
                if samples:
                    # Compute Root-Mean-Square (RMS) amplitude
                    sq_sum = sum(s * s for s in samples)
                    rms = math.sqrt(sq_sum / len(samples))
                    
                    # Create simple visual volume bar (db scale indicator)
                    bar_len = int(rms / 100)
                    if bar_len > 60:
                        bar_len = 60
                    bar = '#' * bar_len + '-' * (60 - bar_len)
                    
                    # Print level (ANSI line-clearing carriage return to stay on single line)
                    print(f"\rAudio Amplitude: [{bar}] {int(rms):<5}", end="")
            time.sleep_ms(30)
    except KeyboardInterrupt:
        print("\nStopping audio capture...")
    finally:
        # De-initialize I2S
        audio_in.deinit()
        print("I2S de-initialized. Test finished.")

if __name__ == "__main__":
    main()
