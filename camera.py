import machine

class Camera:
    def __init__(self):
        self.i2c = None
        self.addr = 0x36
        self.sensor_name = "OV5647"
        self.sda_pin = 7
        self.scl_pin = 8

    def check_id(self):
        """Verify sensor presence by reading the Chip ID on I2C Bus 0 (SDA: 7, SCL: 8)"""
        try:
            self.i2c = machine.I2C(0, sda=machine.Pin(7), scl=machine.Pin(8), freq=100000)
            high = self.i2c.readfrom_mem(0x36, 0x300A, 1, addrsize=16)[0]
            low = self.i2c.readfrom_mem(0x36, 0x300B, 1, addrsize=16)[0]
            cid = (high << 8) | low
            if cid == 0x5647:
                return True
        except Exception:
            pass
        return False

    def capture(self):
        print("Camera (OV5647): Capture not yet supported! Requires CSI/ISP bindings.")
        return None

if __name__ == "__main__":
    cam = Camera()
    if cam.check_id():
        print(f"Camera: {cam.sensor_name} sensor found on address {hex(cam.addr)} (SDA:{cam.sda_pin}, SCL:{cam.scl_pin})!")
    else:
        print("Camera: Sensor NOT found or power/pins incorrect.")
