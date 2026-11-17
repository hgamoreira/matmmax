/* Exercise the served editor against the actual native console/engine. */
'use strict';
const assert=require('node:assert/strict');
const fs=require('node:fs');
module.exports=async function checkEditor(page,command){
 const editor=page.locator('#program'),message=page.locator('#programstate');
 async function run(source,expected='Program finished.'){
  await editor.fill(source);await page.locator('#runprogram').click();
  await page.waitForFunction(expected=>document.getElementById('programstate').textContent===expected,expected);
  assert.equal(await page.locator('#command').isEnabled(),true);
 }
 async function choose(name,source,accept=true){
  const dialog=async d=>accept?d.accept():d.dismiss();page.once('dialog',dialog);
  await page.locator('#file').setInputFiles({name,mimeType:'text/plain',buffer:Buffer.from(source)});
  await page.waitForFunction(()=>!document.getElementById('saveprogram').disabled);
  page.removeListener('dialog',dialog);
 }
 await page.waitForFunction(()=>!document.getElementById('runprogram').disabled);
 await run('s=0;\nfor k=1:8\n    s=s+k;\nend\ns\n');
 assert.match(await command('s'),/s = 36/);
 await run('function y=twice(z)\n y=2*z;\nend\nb=twice(21);\nb');
 assert.match(await command('b'),/b = 42/);
 await run("disp('error: printed text is not a failure')\na=17;");
 assert.match(await command('a'),/a = 17/);
 await run('a=18;\nmissing_editor_name\na=99;','Error while processing source line 2.');
 assert.match(await command('a'),/a = 18/); // Error halts following lines.
 await run('a=19;\nzeros(9,9)\na=99;','Error while processing source line 2.');
 assert.match(await command('a'),/a = 19/);
 await run('a=19;\nfor k=1:2\n a=missing_editor_name;\nend\nb=99;','Error while processing source line 4.');
 assert.match(await command('b'),/b = 42/);
 await run('a=20;\nfor k=1:8\n a=a+k;','Incomplete block at end of program; add the missing end.');
 assert.equal((await page.locator('#prompt').innerText()).trim(),'>>');
 assert.match(await command('a'),/a = 20/);
 await command('a=23;');
 for(const [source,error] of [
  ['a=99;\n'+('x'.repeat(192)),'Line 2: exceeds 191 characters.'],
  ['a=99;\n% \u03c0','Line 2: use printable ASCII text.'],
  ['a=99;\n:reset','Line 2: use this control command in the console, not in a program.'],
  ['a=99;\nquit','Line 2: use this control command in the console, not in a program.']
 ]){
  await editor.fill(source);await page.locator('#runprogram').click();assert.equal(await message.innerText(),error);
  assert.match(await command('a'),/a = 23/); // Full validation before any line is sent.
 }
 await editor.fill('for k=1:8');await command('for k=1:8');
 await page.locator('#runprogram').click();
 assert.match(await message.innerText(),/Finish the open console block/);
 await page.locator('#stop').click();
 await page.waitForFunction(()=>!document.getElementById('command').disabled);
 await editor.fill('a=24;\nwhile 1\n a=a+1;\nend\nb=99;');
 await page.locator('#runprogram').click();
 await page.waitForFunction(()=>document.getElementById('programstate').textContent.startsWith('Running line 4'));
 assert.equal(await page.locator('#command').isDisabled(),true);
 assert.equal(await page.locator('#program').getAttribute('readonly'),'');
 await page.locator('#programstop').click();
 await page.waitForFunction(()=>document.getElementById('programstate').textContent.startsWith('Program stopped;'));
 assert.match(await command('b'),/b = 42/);
 // Stop immediately, before the first acknowledgment: no queued tail may run.
 await editor.fill('a=25;\n'+('a=a+1;\n'.repeat(80))+'b=99;');
 await page.evaluate(()=>{document.getElementById('runprogram').click();document.getElementById('programstop').click();});
 await page.waitForFunction(()=>!document.getElementById('command').disabled);
 assert.match(await command('b'),/b = 42/);
 // Disconnect terminates the queue and keeps the local source for saving.
 const disconnectedSource='while 1\n a=a+1;\nend\nb=99;';
 await editor.fill(disconnectedSource);await page.locator('#runprogram').click();
 await page.waitForFunction(()=>document.getElementById('programstate').textContent.startsWith('Running line 3'));
 await page.locator('#disconnect').click();
 await page.waitForFunction(()=>document.getElementById('state').textContent==='Disconnected');
 assert.equal(await editor.inputValue(),disconnectedSource);
 assert.match(await message.innerText(),/will not resume automatically/);
 await page.locator('#connect').click();
 await page.waitForFunction(()=>!document.getElementById('command').disabled);
 assert.match(await command('b'),/b = 42/);
 assert.match(await command('x+1'),/ans = 42/);
 // A transport drop also cancels locally; reconnect never replays the source.
 await editor.fill(disconnectedSource);await page.locator('#runprogram').click();
 await page.waitForFunction(()=>document.getElementById('programstate').textContent.startsWith('Running line 3'));
 await page.evaluate(()=>ws.close());
 await page.waitForFunction(()=>document.getElementById('state').textContent==='Disconnected');
 assert.match(await message.innerText(),/Connection lost at source line/);
 assert.equal(await editor.inputValue(),disconnectedSource);
 await page.locator('#connect').click();
 await page.waitForFunction(()=>!document.getElementById('command').disabled);
 assert.match(await command('b'),/b = 42/);

 // Import local text, normalizing BOM, CRLF and tab indentation.
 await choose('loaded.ms','\ufeffs=0;\r\nfor k=1:8\r\n\ts=s+k;\r\nend\r\ns\r\n');
 const normalized='s=0;\nfor k=1:8\n    s=s+k;\nend\ns\n';
 assert.equal(await editor.inputValue(),normalized);
 assert.equal(await page.locator('#filename').inputValue(),'loaded.ms');
 await page.locator('#runprogram').click();
 await page.waitForFunction(()=>document.getElementById('programstate').textContent==='Program finished.');
 assert.match(await command('s'),/s = 36/);
 const saved=page.waitForEvent('download');await page.locator('#saveprogram').click();
 const download=await saved;assert.equal(download.suggestedFilename(),'loaded.ms');
 assert.equal(fs.readFileSync(await download.path(),'utf8'),normalized);
 assert.equal(await page.locator('#dirty').innerText(),'');
 // Reject oversized files without replacing the editor.
 await choose('too-large.ms','x'.repeat(32769));
 assert.match(await message.innerText(),/File exceeds 32 KiB/);
 assert.equal(await editor.inputValue(),normalized);
 // New/Open protect unsaved edits when the confirmation is declined.
 await editor.fill('a=27;');
 page.once('dialog',d=>d.dismiss());await page.locator('#newprogram').click();
 assert.equal(await editor.inputValue(),'a=27;');
 await choose('replacement.ms','a=99;',false);assert.equal(await editor.inputValue(),'a=27;');
 page.once('dialog',d=>d.accept());await page.locator('#newprogram').click();
 assert.equal(await editor.inputValue(),'');assert.equal(await page.locator('#filename').inputValue(),'untitled.ms');
 // Source validation uses the actual served code at every boundary.
 const checks=await page.evaluate(()=>{
  function error(source){try{prepareSource(source);return '';}catch(e){return e.message;}}
  return [error('x'.repeat(32769)),error('\n'.repeat(1024)),error(''),
   prepareSource('x'.repeat(191)).queue[0].text.length,
   prepareSource('\ufeffa=1;\r\n\tb=2;\r').source];
 });
 assert.match(checks[0],/32 KiB/);assert.match(checks[1],/1024/);assert.match(checks[2],/empty/);
 assert.equal(checks[3],191);assert.equal(checks[4],'a=1;\n    b=2;\n');
 await page.evaluate(()=>onReady(new Uint8Array([82,48])));
 assert.equal(await page.locator('#runprogram').isDisabled(),true);
 assert.equal(await page.locator('#command').isEnabled(),true);
 await page.evaluate(()=>onReady(new Uint8Array([82,48,48])));
 assert.equal(await page.locator('#runprogram').isEnabled(),true);
 await editor.fill('a=28;\na');
 await editor.press('Control+Enter');
 await page.waitForFunction(()=>document.getElementById('programstate').textContent==='Program finished.');
 assert.match(await command('a'),/a = 28/);
 // R14 scriptable GPIO and a cancellable long delay through the actual editor.
 await run("pinmode('D4','output',1);\nfor k=1:3\n digitalwrite('D4',0);\n delay(5);\n digitalwrite('D4',1);\n delay(5);\nend\ndigitalread('D4')");
 assert.match(await command("digitalread('D4')"),/ans = 1/);
 assert.match(await command('analogread()'),/ans = 321/); // Native ADC stub only.
 await editor.fill("a=1; digitalwrite('D4',0);\ndelay(60000);\na=999;");
 await page.locator('#runprogram').click();
 await page.waitForFunction(()=>document.getElementById('programstate').textContent.startsWith('Running line 2'));
 await page.locator('#programstop').click();
 await page.waitForFunction(()=>document.getElementById('programstate').textContent.startsWith('Program stopped;'));
 assert.match(await command('a'),/a = 1/);
 assert.match(await command("digitalread('D4')"),/ans = 0/); // Cancellation leaves last write latched.
 await command("digitalwrite('D4',1);");
 const finalDownload=page.waitForEvent('download');await page.locator('#saveprogram').click();
 await finalDownload; // Clear dirty state for the existing reload/restart checks.
 console.log('PASS: browser editor scripts/functions, error halt, full validation, block cleanup, Stop, disconnect, local Open/Save, unsaved edits and old-firmware guard.');
};
