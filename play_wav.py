import os, time, struct
import lvgl as lv
from machine import I2S, Pin

try:
    import waveshare as board
    sck_pin = 12
    ws_pin = 10
    sd_pin = 9
    print("Board detected: Waveshare P4")
except ImportError:
    import elecrow as board
    sck_pin = 22
    ws_pin = 21
    sd_pin = 23
    print("Board detected: Elecrow P4")


def play_audio(path='/sd/gc.wav', volume=0.5):
    """
    volume: 0.0 to 1.0
    """
    is_waveshare = False
    try:
        lv.init()
        board.init()
        # If we successfully imported waveshare, we can identify it
        import waveshare
        is_waveshare = True
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

    # 1. Initialize Codec / Enable Amp
    if is_waveshare:
        import audio_setup
        # Map 0.0 - 1.0 volume parameter to a balanced range of ES8311 (0x90 to 0xDF)
        # 0x00 is silent, 0x90 is -23dB, 0xB7 is -4dB (comfortable), 0xDF is +16dB (loud/distorted)
        if volume <= 0:
            val_reg = 0x00
        else:
            val_reg = 0x90 + int(volume * 79)
        if not audio_setup.init_codec(volume_db=val_reg):
            print("Failed to initialize Waveshare audio codec.")
            return
    else:
        board.speaker_enable(True)

    try:
        with open(path, 'rb') as f:
            # 2. Parse WAV Header
            header = f.read(44)
            if header[0:4] != b'RIFF':
                print("Error: Not a WAV file")
                return

            # channels: offset 22, rate: offset 24, bits: offset 34
            num_channels = struct.unpack('<H', header[22:24])[0]
            sample_rate = struct.unpack('<I', header[24:28])[0]
            bits_per_sample = struct.unpack('<H', header[34:36])[0]
            
            print(f"File Info: {sample_rate}Hz, {bits_per_sample}-bit, {num_channels} channel(s)")
            print(f"Hardware/Software Volume: {int(volume * 100)}%")

            # 3. Setup I2S standard mode
            audio = I2S(0, 
                        sck=Pin(sck_pin), 
                        ws=Pin(ws_pin), 
                        sd=Pin(sd_pin), 
                        mode=I2S.TX, 
                        bits=bits_per_sample, 
                        format=I2S.STEREO if num_channels == 2 else I2S.MONO, 
                        rate=sample_rate, 
                        ibuf=10240)

            # 4. Stream Audio
            while True:
                data = f.read(1024)
                if not data:
                    break
                
                # Apply software scaling ONLY if NOT using Waveshare hardware volume control
                if not is_waveshare and volume < 1.0:
                    samples = list(struct.unpack('<%dh' % (len(data)//2), data))
                    for i in range(len(samples)):
                        samples[i] = int(samples[i] * volume)
                    data = struct.pack('<%dh' % len(samples), *samples)
                
                audio.write(data)

    except Exception as e:
        print("Playback error:", e)
    finally:
        print("Finished.")
        if is_waveshare:
            import audio_setup
            audio_setup.deinit_codec()
        else:
            board.speaker_enable(False)
            
        if 'audio' in locals():
            audio.deinit()

if __name__ == "__main__":
    # Play at 50% volume (which maps to 0xDF, the same as the successful test_audio)
    play_audio('/sd/gc.wav', volume=0.8)
