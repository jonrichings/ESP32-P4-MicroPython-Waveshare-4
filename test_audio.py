import os, time, struct, machine
from machine import I2S, Pin

# Audio Pin Mappings for Waveshare P4
SCK_PIN = 12
WS_PIN = 10
SD_PIN = 9  # DOUT
MCLK_PIN = 13
AMP_PIN = 53

def init_es8311():
    print("Probing I2C for ES8311...")
    try:
        # ES8311 is on I2C bus 0, pins 7/8
        i2c = machine.I2C(0, sda=Pin(7), scl=Pin(8), freq=100000)
        devices = i2c.scan()
        print("I2C scan results:", [hex(d) for d in devices])
        if 0x18 not in devices:
            print("Error: ES8311 codec (0x18) not found on I2C bus.")
            return False
            
        # Read Chip ID
        id_high = i2c.readfrom_mem(0x18, 0xFD, 1)[0]
        id_low = i2c.readfrom_mem(0x18, 0xFE, 1)[0]
        print(f"ES8311 Chip ID: {hex(id_high)} {hex(id_low)}")
        
        # Write register helper
        def write_reg(reg, val):
            i2c.writeto_mem(0x18, reg, bytes([val]))
            
        # 1. Reset
        write_reg(0x00, 0x1F)
        time.sleep_ms(50)
        write_reg(0x00, 0x00) # Release Reset
        
        # 2. Configure default clocks / control (from es8311_open)
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
        
        # 3. Select clock source for internal mclk (SCLK as MCLK)
        write_reg(0x01, 0xBF)
        
        # 4. Configure sample rate settings (for 44.1k/48k in MCLK-less mode)
        write_reg(0x02, 0x18) # DIV_PRE and MULTI_PRE
        write_reg(0x05, 0x00) # ADC/DAC divider
        write_reg(0x03, 0x10) # fs_mode & OSR
        write_reg(0x04, 0x10) # DAC OSR
        write_reg(0x07, 0x00) # lrck_h
        write_reg(0x08, 0xff) # lrck_l
        write_reg(0x06, 0x03) # bclk_div (SCLK divider)
        
        # 5. Format and bits per sample (16-bit standard I2S normal)
        write_reg(0x09, 0x0C) # dac_iface (16-bit, normal I2S, unmuted)
        write_reg(0x0A, 0x0C) # adc_iface (16-bit, normal I2S, unmuted)
        
        # 6. Power up ADC/DAC (from es8311_start)
        write_reg(0x17, 0xBF)
        write_reg(0x0E, 0x02)
        write_reg(0x12, 0x00)
        write_reg(0x14, 0x1A)
        write_reg(0x0D, 0x01)
        write_reg(0x15, 0x40)
        write_reg(0x37, 0x08)
        write_reg(0x45, 0x00)
        
        # 7. Unmute & volume (0xBF = 0dB, 0xDF = ~+16dB)
        write_reg(0x31, 0x00) # Unmute DAC
        write_reg(0x32, 0xDF) # Set DAC volume
        
        # 8. Set internal reference signal (ADCL + DACR)
        write_reg(0x44, 0x50)
        
        print("ES8311 registers initialized successfully!")
        return True
    except Exception as e:
        print("Error initializing ES8311:", e)
        return False

def test_playback(path='/sd/gc.wav', volume=0.5):
    # Enable Audio Amp
    amp = Pin(AMP_PIN, Pin.OUT)
    amp.value(1) # Enable amp
    print("Audio amplifier enabled (GPIO 53)")
    
    # Initialize Codec
    if not init_es8311():
        amp.value(0)
        return

    # Mount SD Card
    from machine import SDCard
    try:
        sd = SDCard(slot=0, width=1)
        os.mount(sd, '/sd')
        print("SD card mounted successfully at /sd")
    except Exception as e:
        print("SD Mount failed/skipped:", e)

    try:
        print(f"Opening WAV file: {path}")
        with open(path, 'rb') as f:
            header = f.read(44)
            if header[0:4] != b'RIFF':
                print("Error: Not a WAV file")
                return

            num_channels = struct.unpack('<H', header[22:24])[0]
            sample_rate = struct.unpack('<I', header[24:28])[0]
            bits_per_sample = struct.unpack('<H', header[34:36])[0]
            
            print(f"WAV Info: {sample_rate}Hz, {bits_per_sample}-bit, {num_channels} channel(s)")
            print(f"Scaling Volume: {int(volume * 100)}%")

            # Setup I2S standard mode
            # Note: For ESP32-P4, to output the MCLK signal we configure it, or we rely on default behaviour.
            # MicroPython machine.I2S automatically enables MCLK if Pin(13) is passed as mclk, or if it's the default.
            # In MicroPython, the machine.I2S constructor on ESP32 supports 'mclk' parameter.
            try:
                audio = I2S(0, 
                            sck=Pin(SCK_PIN), 
                            ws=Pin(WS_PIN), 
                            sd=Pin(SD_PIN), 
                            mclk=Pin(MCLK_PIN),
                            mode=I2S.TX, 
                            bits=bits_per_sample, 
                            format=I2S.STEREO if num_channels == 2 else I2S.MONO, 
                            rate=sample_rate, 
                            ibuf=10240)
                print("I2S initialized successfully with MCLK on Pin 13.")
            except TypeError:
                # If mclk parameter is not supported by this build of MicroPython, fall back
                audio = I2S(0, 
                            sck=Pin(SCK_PIN), 
                            ws=Pin(WS_PIN), 
                            sd=Pin(SD_PIN), 
                            mode=I2S.TX, 
                            bits=bits_per_sample, 
                            format=I2S.STEREO if num_channels == 2 else I2S.MONO, 
                            rate=sample_rate, 
                            ibuf=10240)
                print("I2S initialized without mclk parameter.")

            # Play WAV
            start_time = time.time()
            chunk_count = 0
            while True:
                data = f.read(1024)
                if not data:
                    break
                
                # Apply volume scaling
                if volume < 1.0:
                    samples = list(struct.unpack('<%dh' % (len(data)//2), data))
                    for i in range(len(samples)):
                        samples[i] = int(samples[i] * volume)
                    data = struct.pack('<%dh' % len(samples), *samples)
                
                audio.write(data)
                chunk_count += 1
                if chunk_count % 100 == 0:
                    print(f"Streamed {chunk_count} chunks...")
                    
            print(f"Finished playing in {time.time() - start_time:.2f} seconds.")

    except Exception as e:
        print("Playback error:", e)
    finally:
        amp.value(0)
        print("Audio amplifier disabled.")
        if 'audio' in locals():
            audio.deinit()

if __name__ == "__main__":
    test_playback('/sd/gc.wav', volume=0.3)
