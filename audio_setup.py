import machine, time
from machine import Pin

# Hardware Pin Mappings for Waveshare ESP32-P4
I2S_SCK = 12
I2S_WS = 10
I2S_SD = 9  # DOUT (Data output from ESP32 to Codec DAC)
I2S_MCLK = 13
AMP_PIN = 53
CODEC_ADDR = 0x18

def init_codec(volume_db=0xCF):
    """
    Initializes the onboard ES8311 Audio Codec over I2C in MCLK-less mode
    (deriving master clock from SCLK/BCLK), enables the onboard power amplifier,
    and sets the digital volume.
    
    volume_db: volume byte value from 0x00 (-95.5dB, silent) to 0xFF (+32dB, max).
               0xBF is 0dB (unity gain), 0xCF is ~+10dB, 0xDF is ~+16dB.
    """
    # 1. Enable Speaker Power Amplifier (GPIO 53, active high)
    amp = Pin(AMP_PIN, Pin.OUT)
    amp.value(1)
    
    try:
        # ES8311 is on I2C bus 0, sharing SDA: Pin 7, SCL: Pin 8
        i2c = machine.I2C(0, sda=Pin(7), scl=Pin(8), freq=100000)
        
        def write_reg(reg, val):
            i2c.writeto_mem(CODEC_ADDR, reg, bytes([val]))
            
        # 2. Reset Sequence
        write_reg(0x00, 0x1F)
        time.sleep_ms(50)
        write_reg(0x00, 0x00) # Release Reset
        
        # 3. Configure default clocks / control (matches official C driver)
        write_reg(0x01, 0x30)
        write_reg(0x02, 0x00)
        write_reg(0x03, 0x10)
        write_reg(0x16, 0x24)
        write_reg(0x04, 0x10)
        write_reg(0x05, 0x00)
        write_reg(0x0B, 0x00)
        write_reg(0x0C, 0x00)
        write_reg(0x10, 0x1F)
        write_reg(0x11, 0x7F)
        write_reg(0x00, 0x80) # Slave mode (MSC=0)
        
        # 4. Sourced clock setup: use SCLK (BCLK) pin as master clock source
        write_reg(0x01, 0xBF)
        
        # 5. Configure clock dividers/multipliers for standard 44.1k/48k rates
        write_reg(0x02, 0x18)
        write_reg(0x05, 0x00)
        write_reg(0x03, 0x10)
        write_reg(0x04, 0x10)
        write_reg(0x07, 0x00)
        write_reg(0x08, 0xff)
        write_reg(0x06, 0x03)
        
        # 6. Serial Port Format: 16-bit normal I2S mode, unmuted
        write_reg(0x09, 0x0C)
        write_reg(0x0A, 0x0C)
        
        # 7. Power up DAC and analog outputs
        write_reg(0x17, 0xBF)
        write_reg(0x0E, 0x02)
        write_reg(0x12, 0x00)
        write_reg(0x14, 0x1A)
        write_reg(0x0D, 0x01)
        write_reg(0x15, 0x40)
        write_reg(0x37, 0x08)
        write_reg(0x45, 0x00)
        
        # 8. Unmute DAC & Set volume
        write_reg(0x31, 0x00) # Unmute DAC
        write_reg(0x32, volume_db) # DAC digital volume register
        
        # 9. Set reference signal
        write_reg(0x44, 0x50)
        
        print("Codec ES8311 initialized successfully.")
        return True
    except Exception as e:
        print("Error initializing ES8311 Codec:", e)
        amp.value(0) # Disable amp if we failed
        return False

def set_volume(volume_db):
    """
    Sets the DAC digital volume.
    volume_db: volume byte value from 0x00 (-95.5dB, silent) to 0xFF (+32dB, max).
    """
    try:
        i2c = machine.I2C(0, sda=Pin(7), scl=Pin(8), freq=100000)
        i2c.writeto_mem(CODEC_ADDR, 0x32, bytes([volume_db]))
        return True
    except Exception as e:
        print("Error changing volume:", e)
        return False

def deinit_codec():
    """
    Disables the speaker amplifier power.
    """
    amp = Pin(AMP_PIN, Pin.OUT)
    amp.value(0)
    print("Speaker amplifier disabled.")
