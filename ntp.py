"""
NTP / HTTP / Geolocation time sync for MicroPython on ESP32.
Tries methods in order:
  1. IP-API.com Geolocation (Automated Timezone + Precise Offset)
  2. WorldTimeAPI (Backup Geolocation)
  3. UDP NTP (Precise Time, needs manual offset)
  4. HTTP Date header (Fallback)
"""
import socket
import struct
import time
import machine
import json
import gc

_MP_EPOCH_OFFSET = 946684800
_NTP_EPOCH_OFFSET = 2208988800
_NTP_SERVERS = ['pool.ntp.org', 'time.google.com', 'time.cloudflare.com']
_HTTP_SERVERS = [('worldtimeapi.org', 80, '/api/timezone/UTC'), ('www.google.com', 80, '/')]
_MONTH = {'Jan':1,'Feb':2,'Mar':3,'Apr':4,'May':5,'Jun':6,'Jul':7,'Aug':8,'Sep':9,'Oct':10,'Nov':11,'Dec':12}

_last_detected_offset = None
_last_detected_timezone = "Unknown"

def _sync_via_ip_api():
    """Get time and location via http://ip-api.com/json/"""
    global _last_detected_offset, _last_detected_timezone
    host = 'ip-api.com'
    path = '/json/?fields=status,message,timezone,offset,city,country'
    
    try:
        s = socket.socket()
        s.settimeout(10)
        addr = socket.getaddrinfo(host, 80)[0][-1]
        s.connect(addr)
        req = "GET {} HTTP/1.1\r\nHost: {}\r\nUser-Agent: MicroPython\r\nConnection: close\r\n\r\n".format(path, host)
        s.sendall(req.encode())
        
        buf = b''
        while True:
            chunk = s.recv(1024)
            if not chunk: break
            buf += chunk
        s.close()
        
        res = buf.decode('utf-8', 'ignore')
        json_start = res.find('{')
        if json_start == -1: return None
        
        data = json.loads(res[json_start:])
        if data.get('status') != 'success': return None
        
        # ip-api returns offset in seconds. Convert to hours for our logic.
        offset_sec = data.get('offset', 0)
        _last_detected_offset = offset_sec / 3600.0
        _last_detected_timezone = data.get('timezone', 'Unknown')
        
        # Now we need the actual TIME. 
        # ip-api doesn't give precise current time in the same JSON sometimes, 
        # but we can trigger a high-precision NTP sync now that we have the offset.
        print("Detected Timezone: {} ({})".format(_last_detected_timezone, _last_detected_offset))
        return True
    except Exception as e:
        print("ip-api failed:", e)
        return False

def _sync_via_worldtimeapi():
    global _last_detected_offset, _last_detected_timezone
    host = 'worldtimeapi.org'
    path = '/api/ip'
    try:
        s = socket.socket()
        s.settimeout(10)
        addr = socket.getaddrinfo(host, 80)[0][-1]
        s.connect(addr)
        req = "GET {} HTTP/1.1\r\nHost: {}\r\nConnection: close\r\n\r\n".format(path, host)
        s.sendall(req.encode())
        buf = b''
        while True:
            chunk = s.recv(1024)
            if not chunk: break
            buf += chunk
        s.close()
        res = buf.decode('utf-8', 'ignore')
        json_start = res.find('{')
        if json_start == -1: return None
        data = json.loads(res[json_start:])
        dt_str = data.get('datetime')
        off_str = data.get('utc_offset', '')
        if not dt_str: return None
        
        y, mo, d = int(dt_str[0:4]), int(dt_str[5:7]), int(dt_str[8:10])
        h, mi, se = int(dt_str[11:13]), int(dt_str[14:16]), int(dt_str[17:19])
        
        if off_str:
            sign = -1 if off_str[0] == '-' else 1
            _last_detected_offset = sign * (int(off_str[1:3]) + int(off_str[4:6])/60.0)
            _last_detected_timezone = data.get('timezone', 'Unknown')
            
        rtc = machine.RTC()
        rtc.datetime((y, mo, d, 0, h, mi, se, 0))
        return True
    except: return False

def _sync_via_ntp(timezone_offset):
    global _last_detected_offset
    if _last_detected_offset is not None: timezone_offset = _last_detected_offset
    msg = bytearray(48); msg[0] = 0x1B
    for server in _NTP_SERVERS:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.settimeout(5)
            addr = socket.getaddrinfo(server, 123)[0][-1]
            s.sendto(msg, addr)
            data, _ = s.recvfrom(48)
            s.close()
            if data:
                t = struct.unpack("!I", data[40:44])[0] - _NTP_EPOCH_OFFSET
                t += int(timezone_offset * 3600) - _MP_EPOCH_OFFSET
                tm = time.localtime(t)
                machine.RTC().datetime((tm[0], tm[1], tm[2], tm[6], tm[3], tm[4], tm[5], 0))
                return True
        except: pass
    return False

def _parse_http_date(line):
    try:
        parts = line.strip().split()
        d, mo, y = int(parts[2]), _MONTH.get(parts[3], 0), int(parts[4])
        h, mi, s = [int(x) for x in parts[5].split(':')]
        return (y, mo, d, h, mi, s)
    except: return None

def _sync_via_http(timezone_offset):
    global _last_detected_offset
    if _last_detected_offset is not None: timezone_offset = _last_detected_offset
    for host, port, path in _HTTP_SERVERS:
        try:
            s = socket.socket(); s.settimeout(5)
            addr = socket.getaddrinfo(host, port)[0][-1]
            s.connect(addr)
            s.sendall("HEAD {} HTTP/1.1\r\nHost: {}\r\nConnection: close\r\n\r\n".format(path, host).encode())
            buf = b''
            while True:
                chunk = s.recv(256)
                if not chunk or b'\r\n\r\n' in buf: break
                buf += chunk
            s.close()
            for line in buf.decode('utf-8', 'ignore').split('\r\n'):
                if line.startswith('Date:'):
                    dt = _parse_http_date(line[5:])
                    if dt:
                        y, mo, d, h, mi, se = dt
                        h_adj = int(h + timezone_offset)
                        if h_adj >= 24: h_adj -= 24; d += 1
                        elif h_adj < 0: h_adj += 24; d -= 1
                        machine.RTC().datetime((y, mo, d, 0, h_adj, mi, se, 0))
                        return True
        except: pass
    return False

def set_time(timezone_offset=0, aggressive_retries=1):
    gc.collect()
    
    # 1. Try Professional Geolocation (ip-api.com) to get timezone/offset
    print("Auto-detecting Location/Timezone...")
    if _sync_via_ip_api():
        # Successfully got offset, now get precise time via NTP
        if _sync_via_ntp(0): # NTP will use _last_detected_offset
            print("Auto-sync Successful: {} ({})".format(time.localtime(), _last_detected_timezone))
            return True
            
    # 2. Fallback to WorldTimeAPI (Time + Offset in one)
    print("Falling back to WorldTimeAPI...")
    if _sync_via_worldtimeapi(): return True
    
    # 3. Fallback to Manual NTP
    print("Falling back to Manual NTP...")
    if _sync_via_ntp(timezone_offset): return True
    
    # 4. Final Fallback to HTTP
    return _sync_via_http(timezone_offset)

def get_timezone_name():
    return _last_detected_timezone
