const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),crypto=require('node:crypto'),path=require('node:path');
const html=fs.readFileSync(path.join(__dirname,'../src/web_ui.h'),'utf8');
const script=html.split('<script>')[1].split('</script>')[0];
new vm.Script(script);
const helpers=script.slice(script.indexOf('function firmwareSha256'),script.indexOf('function renderUpdateState'));
const sandbox={};vm.createContext(sandbox);vm.runInContext(helpers,sandbox);
for(const size of [0,1,3,55,56,63,64,65,1000,16384,1176896]){
  const data=size===3?Buffer.from('abc'):crypto.randomBytes(size);
  assert.equal(sandbox.firmwareSha256(data),crypto.createHash('sha256').update(data).digest('hex'),'SHA-256 size '+size);
}
assert.equal(sandbox.newerFirmware('0.10.0','0.9.9'),true);
assert.equal(sandbox.newerFirmware('0.4.0','0.4.0'),false);
assert.equal(sandbox.newerFirmware('0.3.1','0.4.0'),false);
assert.equal(sandbox.newerFirmware('0.5.0-beta','0.4.0'),false);
const manifest={schema:1,project:'diesel-heater-gateway',target:'xiao_esp32s3',firmwareId:'DIESELHEATER_GATEWAY:XIAO_ESP32S3:OTA1',file:'firmware-app-at-0x10000.bin',version:'0.4.0',size:1200000,sha256:'a'.repeat(64)};
assert.equal(sandbox.validateFirmwareManifest(manifest,'v0.4.0',3342336),manifest);
for(const change of [{target:'esp8266'},{file:'https://evil.example/app.bin'},{size:9999999},{sha256:'bad'},{version:'0.4.1'},{firmwareId:'other'},{schema:2}])assert.throws(()=>sandbox.validateFirmwareManifest({...manifest,...change},'v0.4.0',3342336));
console.log('PASS: JavaScript syntax, SHA-256, version comparison and incompatible release rejection');
