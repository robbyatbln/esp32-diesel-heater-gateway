"""Build and package the app BEFORE committing and tagging a release."""
from pathlib import Path
import hashlib, json, re, subprocess, sys

root=Path(__file__).resolve().parents[1]
subprocess.run([sys.executable,'-m','platformio','run','-d',str(root)],check=True)
version=re.search(r'VERSION\[\] = "(\d+\.\d+\.\d+)"',(root/'src/main.cpp').read_text(encoding='utf-8')).group(1)
data=(root/'.pio/build/xiao_s3/firmware.bin').read_bytes()
identity='DIESELHEATER_GATEWAY:XIAO_ESP32S3:OTA1'
assert data[0]==0xe9 and data[12:14]==b'\x09\x00' and data[32:36]==bytes.fromhex('3254cdab')
assert identity.encode() in data and len(data)<=0x330000
name='firmware-app-at-0x10000.bin'
(root/name).write_bytes(data)
manifest=dict(schema=1,project='diesel-heater-gateway',target='xiao_esp32s3',firmwareId=identity,version=version,file=name,size=len(data),sha256=hashlib.sha256(data).hexdigest())
(root/'update-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
print(f'Ready for commit and tag v{version}: {len(data)} bytes')
