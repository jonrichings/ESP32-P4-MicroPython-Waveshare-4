import machine
import time

print("Starting raw touch test...")

# 1. Reset sequence for GT911 touch chip
rst = machine.Pin(40, machine.Pin.OUT)
t_int = machine.Pin(42, machine.Pin.OUT)

# Pull both low
rst.value(0)
t_int.value(0)
time.sleep_ms(20)

# Pull Reset high while INT is low (forces GT911 to I2C address 0x5D)
rst.value(1)
time.sleep_ms(10)

# Set INT back to input
t_int.init(machine.Pin.IN)
time.sleep_ms(50)

# 2. Initialize software I2C (SoftI2C) for bit-banging stability
i2c = machine.SoftI2C(sda=machine.Pin(7), scl=machine.Pin(8), freq=100000)


# 3. Read ID
try:
    chip_id = i2c.readfrom_mem(0x5D, 0x8140, 4, addrsize=16)
    print("Success! Verified Chip ID via raw I2C:", chip_id)
except Exception as e:
    print("Could not read Chip ID:", e)

# 4. Poll touch coordinates for 10 seconds
print("\nPolling touch registers for 10 seconds...")
start_time = time.time()
while time.time() - start_time < 10:
    try:
        # 4a. Read ID register to check if bus is still alive
        chip_id = i2c.readfrom_mem(0x5D, 0x8140, 4, addrsize=16)
        print("Bus alive! ID:", chip_id)
        
        # 4b. Read touch status register
        status = i2c.readfrom_mem(0x5D, 0x814E, 1, addrsize=16)[0]
        print(f"Status register: {hex(status)}")
        
        # Check if coordinates are ready (bit 7)
        if status & 0x80:
            points = status & 0x0F
            data = i2c.readfrom_mem(0x5D, 0x814F, 6, addrsize=16)
            x = data[0] | (data[1] << 8)
            y = data[2] | (data[3] << 8)
            print(f"RAW Python Touch -> Points: {points}, X: {x}, Y: {y}")
            # Clear status flag
            i2c.writeto_mem(0x5D, 0x814E, b'\x00', addrsize=16)
    except Exception as e:
        print("I2C Poll Error:", e)

        
    time.sleep_ms(50)

print("\nRaw touch test complete.")
