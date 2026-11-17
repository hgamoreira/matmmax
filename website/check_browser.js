/* Optional visual check: existing Playwright + Chrome; no npm install needed. */
'use strict';
const {chromium}=require(process.env.PLAYWRIGHT_NODE_MODULE||'playwright');
const http=require('node:http');
const fs=require('node:fs');
const path=require('node:path');
const assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../build/github-pages');
const server=http.createServer((req,res)=>{
 const file={'/':'index.html','/index.html':'index.html','/style.css':'style.css'}[req.url];
 if(!file){res.writeHead(404);res.end();return;}
 res.setHeader('Content-Type',file.endsWith('.css')?'text/css':'text/html');
 res.end(fs.readFileSync(path.join(root,file)));
});
(async()=>{
 let browser;
 try{
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const url=`http://127.0.0.1:${server.address().port}`;
  browser=await chromium.launch({headless:true,executablePath:process.env.CHROME||'/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage']});
  for(const [name,width,height] of [['desktop',1440,1100],['mobile',390,844]]){
   const page=await browser.newPage({viewport:{width,height}});
   const errors=[];page.on('pageerror',e=>errors.push(e.message));
   const response=await page.goto(url);
   assert.equal(response.status(),200);
   assert.equal(await page.locator('h1').count(),1);
   assert.equal(await page.locator('.card').count(),2);
   assert(await page.locator('#start').count());
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'Page overflows on '+name);
   await page.locator('.button').click();
   await page.waitForURL('**/#start');
   assert.deepEqual(errors,[]);
   await page.screenshot({path:path.join(root,`preview-${name}.png`),fullPage:true});
   await page.close();
  }
  console.log('PASS: HTTP 200, desktop/mobile layout, navigation, no horizontal overflow or page errors.');
 }finally{if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));}
})().catch(error=>{console.error(error);process.exitCode=1;});
