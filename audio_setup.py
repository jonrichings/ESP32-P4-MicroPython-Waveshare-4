import machine, time
from machine import Pin
import waveshare

def init_codec(volume_db=0xCF):
    """
    Initializes the onboard ES8311 Audio Codec using C driver I2C bindings,
    enables the onboard power amplifier, and sets the digital volume.
    """
    try:
        # Check if codec is reachable first
        if not waveshare.check_codec():
            print("Error: ES8311 codec not detected on the I2C bus.")
            return False
        
        # Initialize register states in C
        return waveshare.init_codec(volume_db)
    except Exception as e:
        print("Error initializing ES8311 Codec in C:", e)
        waveshare.speaker_enable(False)
        return False

def set_volume(volume_db):
    """
    Sets the DAC digital volume.
    volume_db: volume byte value from 0x00 to 0xFF.
    """
    try:
        waveshare.set_codec_volume(volume_db)
        return True
    except Exception as e:
        print("Error changing volume in C:", e)
        return False

def deinit_codec():
    """
    Disables the speaker amplifier power.
    """
    waveshare.speaker_enable(False)
    print("Speaker amplifier disabled.")
