// Optional DOM smoke test: npm install --prefix /tmp/vehicle-ui-check jsdom
// NODE_PATH=/tmp/vehicle-ui-check/node_modules node tests/test_ui.cjs
// HTTP server must run in the same network environment.
const assert = require('node:assert/strict');
const {JSDOM, VirtualConsole} = require('jsdom');
const base = process.env.TEST_URL || 'http://127.0.0.1:8080';
const sleep = ms => new Promise(resolve => setTimeout(resolve,ms));
async function waitFor(fn,label) {
  for(let i=0;i<60;i++){if(fn())return;await sleep(100);}
  throw Error('Timed out: '+label);
}
async function command(action,extra={}) {
  const r=await fetch(base+'/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action,...extra})});
  const data=await r.json();assert.equal(r.ok,true,JSON.stringify(data));return data;
}
(async()=>{
  await command('reset');
  const errors=[],console=new VirtualConsole();console.on('jsdomError',error=>errors.push(error.message));
  const dom=await JSDOM.fromURL(base,{runScripts:'dangerously',resources:'usable',pretendToBeVisual:true,virtualConsole:console,
    beforeParse(window){
      window.fetch=(path,options)=>fetch(new URL(path,base),options);
      window.HTMLCanvasElement.prototype.getContext=()=>new Proxy({}, {get:(o,p)=>o[p]||(()=>{}),set:(o,p,v)=>(o[p]=v,true)});
    }});
  const d=dom.window.document;
  try {
    await waitFor(()=>d.getElementById('connection-label').textContent==='Runtime connected','runtime connection');
    const initial=await command('snapshot'),initialCount=initial.ecus.length;
    assert.equal(d.querySelectorAll('.ecu-node').length,initialCount);
    const templateCount=Object.keys(await (await fetch(base+'/api/templates')).json()).length;
    await waitFor(()=>d.querySelectorAll('#template-list button').length===templateCount,'ECU templates');
    const throttle=d.getElementById('throttle');throttle.value='60';throttle.dispatchEvent(new dom.window.Event('input'));
    await waitFor(()=>d.getElementById('throttle-value').textContent==='60%','throttle control');await sleep(100);
    await command('step',{count:1000});
    await waitFor(()=>parseFloat(d.getElementById('speed').textContent)>50,'decoded speed');
    d.querySelector('.tabs [data-tab=analyze]').click();
    await waitFor(()=>d.querySelectorAll('#frames tr').length>0,'CAN table');
    d.querySelector('#frames tr').click();assert.ok(d.querySelector('#decoded-frame h2'));
    d.querySelector('.tabs [data-tab=diagnose]').click();d.getElementById('send-uds').click();
    await waitFor(()=>d.getElementById('uds-result').textContent.includes('62 f1 90'),'multi-frame VIN');
    d.querySelector('.tabs [data-tab=design]').click();d.getElementById('generate').click();
    await waitFor(()=>d.getElementById('validation-badge').textContent==='VALID','ECU builder');
    assert.ok(d.getElementById('provider-note').textContent.includes('offline template'));
    d.getElementById('add-ecu').click();
    await waitFor(()=>d.querySelectorAll('.ecu-node').length===initialCount+1,'imported ECU on canvas');
    assert.deepEqual(errors,[]);
    process.stdout.write('UI smoke checks passed: runtime, controls, telemetry, CAN decoder, UDS, builder, added ECU.\n');
  } finally {dom.window.close();}
})().catch(error=>{process.stderr.write(error.stack+'\n');process.exitCode=1;});
