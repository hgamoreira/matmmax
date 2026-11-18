/* Optional real-browser test. Uses an existing Playwright installation and Chrome. */
'use strict';
const {chromium}=require(process.env.PLAYWRIGHT_NODE_MODULE||'playwright');
const {spawn}=require('node:child_process');
const path=require('node:path');
const fs=require('node:fs');
const assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../..');
const settingsDir=fs.mkdtempSync(path.join(root,'build/esp8266/l01_webrepl/browser-settings-'));
let log='',browser,device;
function startDevice(env={}){
 log='';device=spawn(path.join(root,'build/esp8266/l01_webrepl/native/mathsmith_light'),[],{
  env:{...process.env,MS_NATIVE_WIFI:'1',MS_SETTINGS_FILE:path.join(settingsDir,'settings.bin'),...env},stdio:['pipe','pipe','pipe']});
 device.stdout.on('data',data=>{log=(log+data).slice(-65536);});
 device.stderr.on('data',data=>{log=(log+data).slice(-65536);});
}
startDevice();
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
(async()=>{
 try {
  let match;
  for(let i=0;i<500&&!match;i++){await pause(10);match=log.match(/Browser URL: (http:\/\/127\.0\.0\.1:\d+\/#([a-f0-9]{32}))/);}
  assert(match,'Native server failed to start: '+log);
  browser=await chromium.launch({headless:true,executablePath:process.env.CHROME||'/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage','--no-proxy-server','--host-resolver-rules=MAP matmmax.local 127.0.0.1']});
  const page=await browser.newPage({viewport:{width:1100,height:850},acceptDownloads:true});
  const errors=[];page.on('pageerror',error=>errors.push(error.message));
  await page.goto(match[1].replace('127.0.0.1','matmmax.local'));
  assert.equal(await page.locator('#key').inputValue(),match[2]);
  assert.equal(new URL(page.url()).hash,'');
  await page.locator('#remember').check();
  await page.locator('#connect').click();
  await page.waitForFunction(()=>document.getElementById('state').textContent==='Ready');
  await page.locator('#remember').uncheck();
  assert.equal(await page.evaluate(()=>localStorage.getItem('matmmax.access-key')),null);
  await page.locator('#remember').check();
  assert.equal(await page.evaluate(()=>localStorage.getItem('matmmax.access-key')),match[2]);
  async function command(s){
   await page.locator('#command').fill(s);await page.locator('#run').click();
   await page.waitForFunction(()=>document.getElementById('state').textContent==='Ready');
   return page.locator('#output').innerText();
  }
  await command('x=41;');
  assert.match(await command('x+1'),/ans = 42/);
  await page.locator('#command').press('ArrowUp');
  assert.equal(await page.locator('#command').inputValue(),'x+1');
  await command(':pin D1 output 0');
  assert.match(await command(':write D1 1'),/D1 = 1/);
  await command("disp('<img src=x onerror=alert(1)>')");
  assert.equal(await page.locator('img').count(),0);
  await command('s=0;');await command('for k=1:100000000');
  assert.equal((await page.locator('#prompt').innerText()).trim(),'..>');
  await command('s=s+k;');
  await page.locator('#command').fill('end');await page.locator('#run').click();
  await page.waitForTimeout(50);await page.locator('#stop').click();
  await page.waitForFunction(()=>document.getElementById('state').textContent==='Ready');
  assert.match(await page.locator('#output').innerText(),/interrupted/i);
  assert.match(await command('x+1'),/ans = 42/);
  await require('./check_editor.js')(page,command);
  const downloadEvent=page.waitForEvent('download');await page.locator('#download').click();
  const download=await downloadEvent;
  assert.match(fs.readFileSync(await download.path(),'utf8'),/ans = 42/);
  await page.locator('#disconnect').click();
  await page.waitForFunction(()=>document.getElementById('state').textContent==='Disconnected');
  await page.locator('#connect').click();
  await page.waitForFunction(()=>document.getElementById('state').textContent==='Ready');
  assert.match(await command('x+1'),/ans = 42/);
  await page.screenshot({path:path.join(root,'build/esp8266/l01_webrepl/browser.png'),fullPage:true});
  await page.setViewportSize({width:375,height:812});
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=window.innerWidth),'Mobile page overflows horizontally');
  await page.screenshot({path:path.join(root,'build/esp8266/l01_webrepl/browser-mobile.png'),fullPage:true});
  await page.setViewportSize({width:1100,height:850});
  await page.locator('#disconnect').click();
  await page.waitForFunction(()=>document.getElementById('state').textContent==='Disconnected');
  const port=new URL(page.url()).port;
  const exited=new Promise(resolve=>device.once('exit',resolve));device.kill('SIGTERM');await exited;
  startDevice({MS_NATIVE_WIFI:'0',MS_NATIVE_PORT:port,MS_TEST_EXPECT_SSID:'native',MS_TEST_EXPECT_PASSWORD:''});
  for(let i=0;i<500&&!log.includes('Browser URL:');i++)await pause(10);
  assert(log.includes('Saved Wi-Fi found; reconnecting automatically.'),log);
  assert(log.includes('#'+match[2]),'Browser key changed on reboot');
  assert(!log.includes('No saved network'),log);
  await page.reload();
  assert.equal(await page.locator('#key').inputValue(),match[2]);
  assert.equal(await page.locator('#remember').isChecked(),true);
  await page.locator('#connect').click();
  await page.waitForFunction(()=>document.getElementById('state').textContent==='Ready');
  assert.match(await command('2+2'),/ans = 4/);
  assert.match(await command('x'),/undefined/i); // Workspace stays RAM-only.
  await page.locator('#remember').uncheck();
  await page.locator('#disconnect').click();
  await page.waitForFunction(()=>document.getElementById('state').textContent==='Disconnected');
  await page.reload();assert.equal(await page.locator('#key').inputValue(),'');
  assert.deepEqual(errors,[]);
  console.log('PASS: Chrome UI, hostname/Origin, math, GPIO stubs, Stop, transcript, saved-key reload and automatic join across process restart without serial setup.');
  console.log('Native HAL only; ESP8266 memory, radio and physical pins remain unvalidated.');
 } finally {
  if(browser)await browser.close();if(device)device.kill('SIGTERM');
  fs.rmSync(settingsDir,{recursive:true,force:true});
 }
})().catch(error=>{console.error(error);process.exitCode=1;});
