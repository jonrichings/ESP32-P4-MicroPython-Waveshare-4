import os, time, struct
import lvgl as lv
from machine import I2S, Pin
import waveshare as board
import audio_setup

sck_pin = 12
ws_pin = 10
sd_pin = 9
print("Board: Waveshare P4 (4-inch)")

def play_audio(path='/sd/gc.wav', volume=0.5):
    """
    volume: 0.0 to 1.0
    """
    try:
        lv.init()
        board.init()
    except Exception as e:
        print("Board init skipped/failed:", e)

    # Mount SD Card
    from machine import SDCard
    try:
        sd = SDCard(slot=0, width=1)
        os.mount(sd, '/sd')
        print("SD card mounted.")
    except Exception as e:
        print("SD Mount failed/skipped:", e)

    # Initialize Codec / Enable Amp
    # Map 0.0 - 1.0 volume parameter to a balanced range of ES8311 (0x90 to 0xDF)
    if volume <= 0:
        val_reg = 0x00
    else:
        val_reg = 0x90 + int(volume * 79)
    if not audio_setup.init_codec(volume_db=val_reg):
        print("Failed to initialize Waveshare audio codec.")
        return

    try:
        with open(path, 'rb') as f:
            # Parse WAV Header
            header = f.read(44)
            if header[0:4] != b'RIFF':
                print("Error: Not a WAV file")
                return

            num_channels = struct.unpack('<H', header[22:24])[0]
            sample_rate = struct.unpack('<I', header[24:28])[0]
            bits_per_sample = struct.unpack('<H', header[34:36])[0]
            
            print(f"File Info: {sample_rate}Hz, {bits_per_sample}-bit, {num_channels} channel(s)")
            print(f"Hardware Volume: {int(volume * 100)}%")

            # Setup I2S standard mode
            audio = I2S(0, 
                        sck=Pin(sck_pin), 
                        ws=Pin(ws_pin), 
                        sd=Pin(sd_pin), 
                        mode=I2S.TX, 
                        bits=bits_per_sample, 
                        format=I2S.STEREO if num_channels == 2 else I2S.MONO, 
                        rate=sample_rate, 
                        ibuf=10240)

            # Stream Audio
            while True:
                data = f.read(1024)
                if not data:
                    break
                audio.write(data)

    except Exception as e:
        print("Playback error:", e)
    finally:
        print("Finished.")
        audio_setup.deinit_codec()
        if 'audio' in locals():
            audio.deinit()

if __name__ == "__main__":
    play_audio('/sd/gc.wav', volume=0.8)
