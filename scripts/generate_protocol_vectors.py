"""Regenerate golden vectors using a local checkout of the attributed upstream.

python scripts/generate_protocol_vectors.py /path/to/homeassistant-diesel-heater
The output is committed, so ordinary tests do not execute or download upstream.
"""
from pathlib import Path
import datetime
import json
import random
import sys

reference = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(reference/'diesel_heater_ble/src'))
from diesel_heater_ble import (ProtocolAA55, ProtocolAA55Encrypted, ProtocolAA66,
    ProtocolAA66Encrypted, ProtocolABBA, ProtocolCBFF, ProtocolHcalory, _encrypt_data)

classes = [ProtocolAA55, ProtocolAA55Encrypted, ProtocolAA66, ProtocolAA66Encrypted,
           ProtocolABBA, ProtocolCBFF, ProtocolHcalory, ProtocolHcalory]
mac = 'AABBCCDDEEFF'
rng = random.Random(20260914)
cases = []
samples = {}
def add(line, expected):
    cases.append({'input': line, 'expected': expected})
def put(d, index, value, order):
    d[index:index+2] = (value & 65535).to_bytes(2, order)
def frame(profile, i):
    n = [20,48,20,48,21,47,38,38][profile]
    d = bytearray(n)
    if profile < 4:
        d[:2] = bytes([0xaa, 0x55 if profile < 2 else 0x66])
        d[3], d[4], d[5], d[8] = i%2, i%4, i%7, i%3
        d[9], d[10] = rng.randrange(8,37), rng.randrange(1,10)
        if profile in (1,3):
            put(d,6,1250,'big');put(d,11,124,'big');put(d,13,rng.randrange(-20,250),'big')
            put(d,32,rng.randrange(-250,400),'big');d[35]=i%5
            if profile==3:d[27]=i%2;d[9]=rng.randrange(50,95) if d[27] else d[9]
        else:
            put(d,6,120,'little');put(d,11,124,'little');put(d,13,rng.randrange(0,1900),'little')
            put(d,15,rng.randrange(-20,35) if profile==0 else rng.randrange(0,35),'little')
    elif profile==4:
        d[:2]=bytes.fromhex('abba');d[4]=[0,1,2,4,6][i%5];d[5]=[0,1,255][i%3]
        d[6]=rng.randrange(1,37);d[9]=12;d[10]=i%2;d[11]=rng.randrange(25,100)
        put(d,12,120,'big');put(d,16,150,'little')
    elif profile==5:
        d[:2]=bytes.fromhex('cbff');d[10]=[1,2,5,6][i%4];d[11]=i%4
        d[12]=rng.randrange(1,37);d[13]=rng.randrange(1,11);d[14]=i%7;d[15]=i%4;d[17]=i%2
        put(d,18,rng.randrange(-20,40),'little');put(d,21,1250,'little');put(d,23,124,'little');put(d,25,1200,'little')
    else:
        d[:2]=bytes.fromhex('0002');d[20]=[0,0x43,0x85,0xc1,0xf0][i%5];d[21]=i%4
        d[22]=rng.randrange(8,37);put(d,24,124,'big');put(d,27,1204,'big');put(d,30,214,'big');d[37]=i%2
    return d
def command(profile, cmd, arg, status='-', result=None, fahrenheit=False, day=1, pin=1234):
    proto=classes[profile]()
    if profile==5:proto.set_device_sn(mac)
    if profile>=6:
        proto.set_mvp_version(profile==7)
        proto._uses_fahrenheit=fahrenheit
        proto.set_query_timestamp(datetime.datetime(2026,9,14,13,24,35))
    if status!='-':proto.parse(bytearray.fromhex(status))
    line=f'C {profile} {cmd} {arg} {pin} {mac if profile==5 else "-"} {int(fahrenheit)} 13 24 35 {day} {status}'
    if result is not None:add(line,[result]);return
    if cmd==100:
        data=proto.build_handshake(pin) if profile==5 else proto.build_password_handshake(pin)
    else:data=proto.build_command(4 if cmd==5 and profile<=4 else cmd,arg,pin)
    add(line,[0,data.hex()])

for profile, cls in enumerate(classes):
    for i in range(40):
        plain=frame(profile,i);proto=cls()
        parsed=proto.parse(plain)
        wire=_encrypt_data(plain) if profile in (1,3) else plain
        samples[profile,i]=wire.hex()
        fields=[parsed.get(k,0) for k in ('running_state','running_step','running_mode','error_code','temp_unit')]
        fields += [int('set_level' in parsed),parsed.get('set_level',0),int('set_temp' in parsed),parsed.get('set_temp',0)]
        fields += [parsed.get(k,0) for k in ('cab_temperature','case_temperature','supply_voltage','altitude')]
        add(f'P {profile} {wire.hex()} -',[0]+fields)
        if profile==5:
            encrypted=ProtocolCBFF._encrypt_cbff(wire,mac)
            add(f'P {profile} {encrypted.hex()} {mac}',[0]+fields)
    for n in range([18,48,20,48,21,46,38,38][profile]):
        add(f'P {profile} {samples[profile,0][:n*2] or "-"} -',[1])
    add(f'P {profile} {"00"*64} -',[1])
    for cmd,arg in [(1,0),(2,1),(2,2),(3,0),(3,1),(4,8),(4,21),(4,36),(5,1),(5,10)]:
        if profile==4 and cmd==3:
            command(profile,cmd,arg,samples[4,1] if arg==0 else samples[4,0]);continue
        if profile<=4 and cmd in (4,5):
            # AAxx status mode = i%3; ABBA mode = i%3 + 1.
            idx=(1 if cmd==4 else 0) if profile==4 else (2 if cmd==4 else 1)
            status=samples[profile,idx]
            # Clear the synthetic fault for setpoint command context.
            raw=bytearray.fromhex(status)
            if profile in (1,3):raw=_encrypt_data(raw)
            if profile<4:raw[35 if profile==3 else 4]=0
            if profile in (1,3):raw=_encrypt_data(raw)
            command(profile,cmd,arg,raw.hex());continue
        if profile==5 and cmd in (2,3):
            # i=2 is temperature mode with target + current level.
            # For mode 1 upstream substitutes 5 when switching: use level frame.
            status=samples[5,1] if cmd==2 and arg==1 else samples[5,2]
            command(profile,cmd,arg,status);continue
        command(profile,cmd,arg)
    for cmd,arg in [(3,2),(2,0),(5,0),(5,11),(4,-1),(4,200)]:command(profile,cmd,arg,result=2)
    command(profile,99,0,result=3)
    command(profile,2,3,result=4 if profile==4 else 3)
command(4,3,1,result=4)
command(4,3,1,samples[4,1],result=6)
command(4,3,0,samples[4,2],result=4) # Error status must not toggle.
command(4,3,1,samples[4,3],result=3)
command(4,2,3,samples[4,0])
command(5,3,1,result=4)
command(5,2,2,result=4)
command(5,100,0)
command(7,100,0)
command(7,1,0,result=5,day=0)
command(6,4,68,fahrenheit=True)
command(7,4,104,fahrenheit=True)
for profile in range(5):
    command(profile,4,21,result=4)
    command(profile,5,5,result=4)
for profile in range(8):command(profile,1,0,result=2,pin=10000)
for profile in range(8):command(profile,1,1,result=2)
# Wrong device/key/header must never become a valid CBFF measurement.
encrypted=ProtocolCBFF._encrypt_cbff(bytearray.fromhex(samples[5,0]),mac)
add(f'P 5 {encrypted.hex()} 112233445566',[1])
add(f'P 5 {encrypted.hex()} -',[1])
add(f'C 5 1 0 1234 invalid 0 13 24 35 1 -',[2])
add(f'P 0 {samples[0,0][:36]} -',next(c['expected'] for c in cases if c['input']==f'P 0 {samples[0,0]} -'))
add(f'P 5 {samples[5,0][:92]} -',next(c['expected'] for c in cases if c['input']==f'P 5 {samples[5,0]} -'))
output=Path(__file__).resolve().parents[1]/'tests/protocol_vectors.json'
output.write_text(json.dumps({'upstream_commit':'0de895884f597975f2ef90638bf45e358db00519','cases':cases},indent=2)+'\n',encoding='utf-8')
print(f'Wrote {len(cases)} golden and rejection vectors.')
