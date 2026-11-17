/* Capture the real WebREPL against the native firmware harness for documentation. */
'use strict';
const {chromium}=require(process.env.PLAYWRIGHT_NODE_MODULE||'playwright');
const {spawn}=require('node:child_process');
const path=require('node:path');
const fs=require('node:fs');
const assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../..');
const build=path.join(root,'build/esp8266/l01_webrepl');
const output=path.resolve(process.argv[2]||path.join(root,'esp8266/docs/images/webrepl.png'));
const settings=fs.mkdtempSync(path.join(build,'screenshot-settings-'));
const device=spawn(path.join(build,'native/mathsmith_light'),[],{
 env:{...process.env,MS_NATIVE_WIFI:'1',MS_SETTINGS_FILE:path.join(settings,'settings.bin')},
 stdio:['pipe','pipe','pipe']});
let log='',browser;
device.stdout.on('data',data=>{log=(log+data).slice(-16384);});
device.stderr.on('data',data=>{log=(log+data).slice(-16384);});
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
(async()=>{
 try{
  let match;
  for(let i=0;i<500&&!match;i++){
   await pause(10);
   match=log.match(/Browser URL: (http:\/\/127\.0\.0\.1:\d+\/#([a-f0-9]{32}))/);
  }
  assert(match,'Native WebREPL server did not start. Build the native harness first.');
  browser=await chromium.launch({headless:true,executablePath:process.env.CHROME||'/usr/bin/google-chrome',
   args:['--no-sandbox','--disable-dev-shm-usage','--no-proxy-server']});
  const page=await browser.newPage({viewport:{width:1120,height:1240},acceptDownloads:true});
  const errors=[];
  page.on('pageerror',error=>errors.push(error.message));
  const response=await page.goto(match[1]);
  assert.equal(response.status(),200);
  await page.locator('#connect').click();
  await page.waitForFunction(()=>document.getElementById('state').textContent==='Ready');
  await page.locator('#filename').fill('matrix-demo.ms');
  await page.locator('#program').fill([
   'A=[4 -2;-2 4];',
   'b=[2;8];',
   'x=A\\b',
   'eig(A)',
   'A*x',
  ].join('\n'));
  const downloaded=page.waitForEvent('download');
  await page.locator('#saveprogram').click();
  assert.equal((await downloaded).suggestedFilename(),'matrix-demo.ms');
  await page.locator('#clear').click();
  await page.locator('#runprogram').click();
  await page.waitForFunction(()=>document.getElementById('programstate').textContent==='Program finished.');
  const transcript=await page.locator('#output').innerText();
  assert.match(transcript,/x =\s+2\s+3/);
  assert.match(transcript,/eig\(A\)\s+ans =\s+2(?:\.0+)?\s+6(?:\.0+)?/);
  assert.match(transcript,/A\*x\s+ans =\s+2\s+8/);
  assert(!/error:/i.test(transcript));
  assert.equal(await page.locator('#key').getAttribute('type'),'password');
  assert.equal(new URL(page.url()).hash,'');
  assert.equal(await page.locator('#remember').isChecked(),false);
  assert.deepEqual(errors,[]);
  await page.locator('#key').fill('');
  assert.equal(await page.locator('#key').inputValue(),'');
  assert(!(await page.locator('body').innerText()).includes(match[2]));
  await page.locator('#program').blur();
  await page.locator('#command').blur();
  await page.evaluate(()=>document.getElementById('output').scrollTop=0);
  fs.mkdirSync(path.dirname(output),{recursive:true});
  await page.screenshot({path:output,fullPage:true});
  console.log('PASS: real browser UI, script execution, solve [2;3], eigenvalues [2;6], product [2;8].');
  console.log('Screenshot: '+output+' (native firmware harness; no physical-board claims).');
 }finally{
  if(browser)await browser.close();
  device.kill('SIGTERM');
  fs.rmSync(settings,{recursive:true,force:true});
 }
})().catch(error=>{console.error(error);process.exitCode=1;});
