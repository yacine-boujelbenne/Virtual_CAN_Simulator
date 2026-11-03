// NODE_PATH=/path/to/playwright/node_modules VCLAB_RUNTIME=/path/to/vclab node tests/showcase/browser.cjs
const assert=require('node:assert/strict');
const fs=require('node:fs/promises');
const path=require('node:path');
const {spawn}=require('node:child_process');
const {createRequire}=require('node:module');
const {chromium,expect}=createRequire(path.resolve(__dirname,'../../showcase/package.json'))('@playwright/test');
const root=path.resolve(__dirname,'../..');
const output=process.env.SHOWCASE_EVIDENCE||'/tmp/vclab-showcase-evidence';
(async()=>{
  const server=spawn('python3',['showcase/server.py','--runtime',process.env.VCLAB_RUNTIME||path.join(root,'build/native/vclab'),'--port','0'],{cwd:root,stdio:['ignore','pipe','pipe']});
  let stderr='';server.stderr.on('data',b=>stderr+=b);
  const exited=new Promise(resolve=>server.on('exit',(code,signal)=>resolve({code,signal})));
  let browser;
  try{
    const ready=await new Promise((resolve,reject)=>{let text='';const timer=setTimeout(()=>reject(Error('Server startup timeout: '+stderr)),15000);server.stdout.on('data',b=>{text+=b;if(text.includes('\n')){clearTimeout(timer);try{resolve(JSON.parse(text.split('\n')[0]));}catch(e){reject(e);}}});server.on('exit',code=>{clearTimeout(timer);reject(Error('Server exited '+code+': '+stderr));});});
    browser=await chromium.launch({headless:true});const page=await browser.newPage({viewport:{width:1440,height:1000}}),errors=[];page.on('pageerror',e=>errors.push(e.message));
    const snapshot=async()=>{const r=await page.request.get(ready.url+'/api/state');assert.ok(r.ok());return r.json();};
    await page.goto(ready.url);await expect(page.locator('#native-authority')).toContainText('9/9');await expect(page.locator('.ecu-node')).toHaveCount(9);await expect(page.locator('#native-health')).toContainText('HEALTHY');
    await expect.poll(()=>page.evaluate(()=>nativeMesh?.triangles.length??0)).toBeGreaterThan(100);
    assert.equal((await snapshot()).cluster.controller,'native-cpp');
    await page.locator('#native-actor-speed').fill('36');await page.locator('#native-offset').fill('4');await page.locator('#native-add').click();await expect.poll(async()=>(await snapshot()).traffic.actors.length).toBe(2);
    await page.locator('#native-actor-speed').fill('18');await page.locator('#native-speed').click();await expect.poll(async()=>(await snapshot()).traffic.actors.find(a=>a.id==='web-car-1')?.target_speed).toBe(5);
    await page.locator('#native-offset').fill('0');await page.locator('#native-gap').fill('40');await page.locator('#native-move').click();await expect.poll(async()=>(await snapshot()).traffic.lead_id).toBe('web-car-1');
    await page.locator('#native-remove').click();await expect.poll(async()=>(await snapshot()).traffic.actors.length).toBe(1);
    await page.locator('#native-timegap').fill('2.3');await page.locator('#native-follow').click();await expect.poll(async()=>(await snapshot()).controls.following_time_s).toBe(2.3);
    await page.locator('.native-controls summary').click();await page.locator('#native-mass').fill('1900');await page.locator('#native-ratio').fill('7');await page.locator('#native-physics').click();await expect.poll(async()=>(await snapshot()).physics.mass_kg).toBe(1900);
    const savedHash=(await snapshot()).project_hash;
    await page.locator('#native-project').selectOption('ev_lateral.json');await page.locator('#native-switch').click();await expect.poll(async()=>Boolean((await snapshot()).traffic)).toBe(false);
    await page.locator('#native-project').selectOption('session:1');await page.locator('#native-switch').click();await expect(page.locator('#native-switch')).toBeEnabled();await expect.poll(async()=>(await snapshot()).project_hash).toBe(savedHash);await expect(page.locator('#native-timegap')).toHaveValue('2.3');
    await page.locator('.ecu-node[data-name="EPS ECU"]').click();await page.locator('#native-edit-selected').click();await expect(page.locator('#design')).toHaveClass(/active/);const program=JSON.parse(await page.locator('#definition').inputValue());program.tasks[1].assign.steering_angle=.2;await page.locator('#definition').fill(JSON.stringify(program,null,2));await page.locator('#native-deploy').click();await expect(page.locator('#validation-badge')).toHaveText('DEPLOYED');
    const exportHash=(await snapshot()).project_hash,downloadEvent=page.waitForEvent('download');await page.locator('#export-project').click();const download=await downloadEvent;await page.locator('#import-project').setInputFiles(await download.path());await expect(page.locator('#project-badge')).toHaveText('IMPORTED');await expect.poll(async()=>(await snapshot()).project_hash).toBe(exportHash);
    await page.locator('.tabs [data-tab=simulate]').click();await page.locator('#play').click();await expect.poll(async()=>(await snapshot()).tick).toBeGreaterThan(10);await page.locator('#play').click();await expect.poll(async()=>(await snapshot()).running).toBe(false);const stopped=await snapshot();assert.ok(stopped.world.steering_angle>0);await page.waitForTimeout(180);assert.equal((await snapshot()).tick,stopped.tick);
    for(const name of ['network','analyze','diagnose','performance','signals']){await page.locator(`.tabs [data-tab=${name}]`).click();await expect(page.locator('#'+name)).toHaveClass(/active/);}
    await expect(page.locator('#native-signals')).toContainText('motor_temperature');
    await page.locator('.tabs [data-tab=diagnose]').click();await page.locator('#diagnostic-ecu').selectOption('Motor ECU');await page.locator('#send-uds').click();await expect(page.locator('#uds-result')).toContainText('62 f1 90');
    await page.locator('.tabs [data-tab=design]').click();await page.locator('#builder-prompt').fill('Build a motor observation ECU');await page.locator('#generate').click();await expect(page.locator('#validation-badge')).toHaveText('VALID');await expect(page.locator('#provider-note')).toContainText('offline template');await page.locator('#add-ecu').click();await expect.poll(async()=>(await snapshot()).ecus.length).toBe(10);
    await page.locator('.tabs [data-tab=simulate]').click();await page.locator('#native-temp').fill('110');await page.locator('#native-stress').click();await expect(page.locator('#native-health')).toContainText('MOTOR HOT');
    await fs.mkdir(output,{recursive:true});await page.evaluate(()=>window.scrollTo(0,0));await page.screenshot({path:path.join(output,'web-desktop-viewport.png')});await page.screenshot({path:path.join(output,'web-desktop.png'),fullPage:true});
    await page.setViewportSize({width:390,height:844});await page.evaluate(()=>window.scrollTo(0,0));await expect(page.locator('#native-project')).toBeVisible();assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=window.innerWidth+1),'Mobile layout overflows horizontally');await page.screenshot({path:path.join(output,'web-mobile.png'),fullPage:true});assert.deepEqual(errors,[]);
    const result={passed:true,checks:['nine native workers','native health and 3D mesh','traffic add/update/reposition/remove','following controls','physics deployment','session project switching','canonical project download/import','ECU edit/deploy with steering impact','native Run/Pause','all seven workspaces','ISO-TP VIN browser controls','offline ECU builder and added worker','stress health response','390px responsive layout'],console_errors:errors,controller_pid:ready.controller_pid};await fs.writeFile(path.join(output,'browser.json'),JSON.stringify(result,null,2)+'\n');process.stdout.write(JSON.stringify(result)+'\n');
  }finally{if(browser)await browser.close();server.kill('SIGTERM');const exit=await exited;assert.equal(exit.code,0,stderr);}
})().catch(e=>{process.stderr.write(e.stack+'\n');process.exitCode=1;});
