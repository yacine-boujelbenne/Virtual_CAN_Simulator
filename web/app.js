'use strict';
const $ = id => document.getElementById(id);
const escape = value => String(value).replace(/[&<>"']/g, char => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[char]));
let state, templates={}, selected='Motor ECU', activeTab='simulate', frameSelection, history=[], lastTime=-1, dragging=false, dragNode=null;
const positions=new Map();
async function api(url,body) {
  const result=await fetch(url,body?{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}:{});
  const data=await result.json();
  if(!result.ok || data.error)throw new Error(data.error || 'Request failed');
  return data;
}
function toast(message,error=false) {
  $('toast').textContent=message;$('toast').className='toast show'+(error?' error':'');
  clearTimeout(toast.timer);toast.timer=setTimeout(()=>$('toast').classList.remove('show'),5000);
}
async function command(action,extra={}) {
  const data=await api('/api/command',{action,...extra});
  if(data.ecus){state=data;render();}
  return data;
}
function guarded(fn){return async(...args)=>{try{await fn(...args);}catch(error){toast(error.message,true);}};}
function tab(name) {
  activeTab=name;document.querySelectorAll('.tab').forEach(el=>el.classList.toggle('active',el.id===name));
  document.querySelectorAll('[data-tab]').forEach(el=>el.classList.toggle('active',el.dataset.tab===name));
  if(state)render();
}
document.querySelectorAll('[data-tab]').forEach(el=>el.onclick=()=>tab(el.dataset.tab));
function number(value,decimals=1){return Number(value||0).toFixed(decimals);}
function valuesHTML(values){return Object.entries(values).map(([key,value])=>'<div><small>'+escape(key.replaceAll('_',' '))+'</small><b>'+number(value,Math.abs(value)>100?0:2)+'</b></div>').join('');}
function setOptions(id,values) {
  const element=$(id), previous=element.value;
  if(element.dataset.list===JSON.stringify(values))return;
  element.dataset.list=JSON.stringify(values);element.replaceChildren(...values.map(([value,name])=>{
    const option=document.createElement('option');option.value=value;option.textContent=name;return option;
  }));
  if(values.some(([v])=>v===previous))element.value=previous;
}
function render() {
  const cluster=state.ecus.find(e=>e.name==='Instrument Cluster')?.values || {};
  $('scenario-name').textContent=state.name;
  $('clock').textContent=number(state.time,2)+' s';
  $('play').textContent=state.running?'Ⅱ Pause simulation':'▶ Run simulation';
  $('speed').textContent=number(cluster.display_speed);
  $('rpm').textContent=number(cluster.display_rpm,0);
  $('soc').textContent=number(cluster.display_soc);
  $('speed-meter').style.width=Math.min(100,(cluster.display_speed||0)/2)+'%';
  $('rpm-meter').style.width=Math.min(100,(cluster.display_rpm||0)/160)+'%';
  $('soc-meter').style.width=Math.min(100,cluster.display_soc||0)+'%';
  const dtcs=state.ecus.flatMap(e=>e.dtcs.filter(d=>d.active));
  $('dtc-count').textContent=dtcs.length;
  $('health-text').textContent=dtcs.length?'Fault condition detected':'All systems nominal';
  $('health-text').style.color=dtcs.length?'var(--red)':'var(--cyan)';
  $('bus-load').textContent='Estimated bus load · '+number(state.bus_load_estimate,2)+'%';
  if(state.time<lastTime){history=[];lastTime=-1;}
  if(state.time!==lastTime){history.push([state.time,cluster.display_speed||0,state.world.motor_temperature]);lastTime=state.time;}
  history=history.filter(p=>p[0]>=state.time-30).slice(-350);
  $('ignition').checked=Boolean(state.controls.ignition);
  for(const [id,key] of [['throttle','throttle'],['brake','brake'],['friction','road_friction']]) {
    if(document.activeElement!==$(id))$(id).value=Math.round(state.controls[key]*100);
    $(id+'-value').textContent=id==='friction'?number(state.controls[key],2)+' μ':Math.round(state.controls[key]*100)+'%';
  }
  setOptions('fault-sensor',state.sensors.map(s=>[s.name,s.name.replaceAll('_',' ')+(s.fault!=='none'?' · '+s.fault:'')]));
  setOptions('diagnostic-ecu',state.ecus.map(e=>[e.name,e.name]));
  if(!state.ecus.some(e=>e.name===selected))selected=state.ecus[0].name;
  const ecu=state.ecus.find(e=>e.name===selected);
  $('inspector-name').textContent=ecu.name;$('inspector-state').textContent=ecu.state;
  $('inspector-values').innerHTML=valuesHTML(ecu.values);
  $('inspector-json').textContent=JSON.stringify(ecu.definition,null,2);
  if(activeTab==='simulate'){renderNodes();drawChart();}
  if(activeTab==='analyze')renderFrames();
  if(activeTab==='diagnose')renderDTCs();
}
function renderNodes() {
  const holder=$('ecu-nodes'), area=$('topology'), width=area.clientWidth,height=area.clientHeight;
  const busTop=$('topology').querySelector('.bus-bar').offsetTop;
  for(let i=0;i<state.ecus.length;i++){
    const ecu=state.ecus[i],key=ecu.name;
    let node=[...holder.children].find(n=>n.dataset.name===key);
    if(!node){node=document.createElement('div');node.className='ecu-node';node.dataset.name=key;holder.append(node);node.onpointerdown=startDrag;node.onclick=()=>{if(dragging)return;selected=key;render();};}
    if(!positions.has(key)){
      const top=i<3, column=top?i:i-3;
      const count=top?3:Math.max(2,state.ecus.length-3);
      positions.set(key,{x:(column+.5)/count,y:top?0.04:0.65});
    }
    node.className='ecu-node'+(key===selected?' selected':'')+(ecu.state==='FAULT'?' fault':'');
    const status=ecu.definition.messages.filter(m=>m.direction==='tx').map(m=>'0x'+m.id.toString(16).toUpperCase()).join(' · ');
    const icon=key.includes('Motor')?'⚙':key.includes('ABS')?'◉':key.includes('BMS')?'▰':key.includes('Dynamics')?'↗':'▤';
    const v=ecu.values, display=key.includes('Motor')?number(v.motor_rpm,0)+' rpm / '+number(v.motor_temperature)+' °C':
      key.includes('ABS')?(v.abs_active?'ABS ACTIVE':'READY')+' / '+number(v.pressure*100,0)+'%':
      key.includes('BMS')?number(v.battery_soc)+'% / '+number(v.pack_voltage)+' V':
      key.includes('Dynamics')?number(v.vehicle_speed)+' km/h':number(v.display_speed)+' km/h / '+number(v.display_soc)+'%';
    node.innerHTML='<div class="node-head"><span>'+icon+'</span><span class="node-status">'+escape(ecu.state)+'</span></div><div class="node-name">'+escape(key)+'</div><div class="node-value">'+escape(display)+'</div><div class="node-can">'+escape(status||'CAN receive only')+'</div>';
    const p=positions.get(key);
    if(dragNode!==node){node.style.left=Math.max(0,Math.min(width-node.offsetWidth,width*p.x-node.offsetWidth/2))+'px';node.style.top=Math.max(0,Math.min(height-node.offsetHeight,height*p.y))+'px';}
  }
  for(const node of [...holder.children])if(!state.ecus.some(e=>e.name===node.dataset.name))node.remove();
  let lines='';
  for(const node of holder.children){
    const x=node.offsetLeft+node.offsetWidth/2,y=node.offsetTop;
    const top=y<busTop,anchor=top?y+node.offsetHeight:y;
    lines+='<path d="M'+x+' '+anchor+' L'+x+' '+(busTop+(top?0:29))+'" stroke="'+(node.dataset.name===selected?'#53dfcc':'#3b5667')+'" stroke-width="1.5" fill="none"/>';
    lines+='<circle cx="'+x+'" cy="'+(busTop+(top?0:29))+'" r="3" fill="#53dfcc"/>';
  }
  $('topology-lines').innerHTML=lines;
}
function startDrag(event) {
  const node=event.currentTarget,rect=$('topology').getBoundingClientRect();
  dragNode=node;dragging=false;
  const startX=event.clientX,startY=event.clientY,left=node.offsetLeft,top=node.offsetTop;
  node.setPointerCapture(event.pointerId);
  node.onpointermove=e=>{
    if(Math.abs(e.clientX-startX)+Math.abs(e.clientY-startY)>5)dragging=true;
    if(!dragging)return;
    const x=Math.max(0,Math.min(rect.width-node.offsetWidth,left+e.clientX-startX));
    const y=Math.max(0,Math.min(rect.height-node.offsetHeight,top+e.clientY-startY));
    positions.set(node.dataset.name,{x:(x+node.offsetWidth/2)/rect.width,y:y/rect.height});
    node.style.left=x+'px';node.style.top=y+'px';renderNodes();
  };
  node.onpointerup=()=>{node.onpointermove=null;dragNode=null;setTimeout(()=>dragging=false,100);};
}
function drawChart() {
  const canvas=$('chart'),w=canvas.clientWidth,h=canvas.clientHeight;if(!w)return;
  const scale=window.devicePixelRatio||1;canvas.width=w*scale;canvas.height=h*scale;
  const ctx=canvas.getContext('2d');ctx.scale(scale,scale);ctx.clearRect(0,0,w,h);
  const left=30,right=w-9,top=8,bottom=h-18;
  ctx.font='9px monospace';ctx.fillStyle='#61788c';ctx.strokeStyle='#22303e';ctx.lineWidth=1;
  for(let i=0;i<5;i++){const y=top+(bottom-top)*i/4;ctx.beginPath();ctx.moveTo(left,y);ctx.lineTo(right,y);ctx.stroke();ctx.fillText(String(200-i*50),0,y+3);}
  const end=Math.max(30,state?.time||0),start=end-30;
  for(let i=0;i<=5;i++){const x=left+(right-left)*i/5;ctx.fillText(number(start+i*6,0)+'s',x,bottom+14);}
  for(const [column,max,color] of [[1,200,'#53dfcc'],[2,160,'#a694ff']]){
    ctx.strokeStyle=color;ctx.lineWidth=2;ctx.beginPath();
    history.forEach((point,index)=>{const x=left+(point[0]-start)/30*(right-left),y=bottom-Math.min(1,Math.max(0,point[column]/max))*(bottom-top);index?ctx.lineTo(x,y):ctx.moveTo(x,y);});ctx.stroke();
  }
}
function renderFrames() {
  const filter=$('frame-filter').value.toLowerCase();
  const frames=state.frames.slice().reverse().filter(f=>JSON.stringify({...f,hex:'0x'+f.id.toString(16)}).toLowerCase().includes(filter));
  $('frame-counter').textContent=state.frame_count+' total frames · '+frames.length+' shown';
  $('frames').innerHTML=frames.map((f,index)=>'<tr data-index="'+index+'"><td>'+number(f.time,3)+'</td><td>0x'+f.id.toString(16).toUpperCase()+'</td><td>'+escape(f.ecu)+'</td><td>'+escape(f.name)+'</td><td>'+f.dlc+'</td><td>'+escape(f.bytes)+'</td></tr>').join('');
  $('frames').querySelectorAll('tr').forEach(row=>row.onclick=()=>{frameSelection=frames[Number(row.dataset.index)];renderDecoded();});
  $('events').innerHTML=state.events.slice().reverse().map(e=>'<div><small>'+number(e.time,2)+' s · '+escape(e.ecu)+'</small>'+escape(e.from)+' → '+escape(e.to)+'</div>').join('')||'<p>Run or step the simulation to see transitions.</p>';
}
function renderDecoded(){const f=frameSelection;$('decoded-frame').innerHTML='<h2>0x'+f.id.toString(16).toUpperCase()+' · '+escape(f.name)+'</h2><div class="value-list">'+valuesHTML(f.signals)+'</div>';}
function renderDTCs() {
  const codes=state.ecus.flatMap(e=>e.dtcs.map(d=>({...d,ecu:e.name})));
  $('dtcs').innerHTML=codes.map(d=>'<div class="dtc'+(!d.active?' recovered':'')+'"><div class="dtc-header"><strong>'+escape(d.code)+'</strong><span class="chip">'+(d.active?'ACTIVE':'STORED / RECOVERED')+'</span></div><h2>'+escape(d.name)+'</h2><p>'+escape(d.ecu)+' · '+escape(d.severity)+'</p></div>').join('')||'<div class="empty">No diagnostic trouble codes.<br>Inject a sensor fault to test the detection chain.</div>';
}
$('play').onclick=guarded(()=>command(state.running?'pause':'run'));
$('step').onclick=guarded(async()=>{await command('pause');await command('step');});
$('reset').onclick=guarded(async()=>{await command('reset');frameSelection=null;toast('Scenario reset.');});
let controlTimer;
for(const [id,key] of [['throttle','throttle'],['brake','brake'],['friction','road_friction']]){
  $(id).oninput=()=>{const value=Number($(id).value)/100;$(id+'-value').textContent=id==='friction'?number(value,2)+' μ':Math.round(value*100)+'%';clearTimeout(controlTimer);controlTimer=setTimeout(guarded(()=>command('controls',{values:{throttle:Number($('throttle').value)/100,brake:Number($('brake').value)/100,road_friction:Number($('friction').value)/100}})),40);};
}
$('ignition').onchange=guarded(()=>command('controls',{values:{ignition:$('ignition').checked?1:0}}));
$('inject').onclick=guarded(async()=>{await command('fault',{sensor:$('fault-sensor').value,mode:$('fault-mode').value,value:Number($('fault-value').value)});toast('Fault injected. Run or step to observe the response.');});
$('clear-fault').onclick=guarded(async()=>{await command('fault',{sensor:$('fault-sensor').value,mode:'none'});toast('Sensor restored. Stored DTCs remain until cleared.');});
$('abs-demo').onclick=guarded(async()=>{
  await command('reset');await command('controls',{values:{throttle:.7}});await command('step',{count:1000});await command('step',{count:1000});
  await command('controls',{values:{throttle:0,brake:1,road_friction:.2}});await command('run');toast('Low-friction braking active. Inspect the ABS ECU and BrakeStatus frames.');
});
$('frame-filter').oninput=()=>state&&renderFrames();
$('uds-preset').onchange=()=>$('uds-bytes').value=$('uds-preset').value;
$('send-uds').onclick=guarded(async()=>{
  const tokens=$('uds-bytes').value.trim().split(/\s+/);
  if(!tokens.every(t=>/^[0-9a-fA-F]{2}$/.test(t)))throw new Error('Use two-digit hex bytes separated by spaces.');
  const response=await command('diagnostic',{ecu:$('diagnostic-ecu').value,bytes:tokens.map(t=>parseInt(t,16))});
  $('uds-result').textContent='TX 0x7E0   '+response.request+'\nRX 0x7E8   '+response.response+'\n\n'+response.transport;
  await poll();
});
function editDefinition(definition){$('definition').value=JSON.stringify(definition,null,2);$('validation-badge').textContent='UNVALIDATED';}
$('definition').oninput=()=>$('validation-badge').textContent='UNVALIDATED';
$('validate').onclick=guarded(async()=>{await command('validate',{definition:JSON.parse($('definition').value)});$('validation-badge').textContent='VALID';$('validation-result').textContent='Encoding, timing, state machine, and expressions passed. Adding checks the full network.';toast('Definition is valid.');});
$('add-ecu').onclick=guarded(async()=>{await command('add_ecu',{definition:JSON.parse($('definition').value)});toast('ECU added. Export JSON to preserve its definition.');tab('simulate');});
$('generate').onclick=guarded(async()=>{
  const button=$('generate');button.disabled=true;
  try{const result=await api('/api/generate',{prompt:$('builder-prompt').value});editDefinition(result.definition);$('validation-badge').textContent='VALID';$('provider-note').textContent='Generated with '+result.provider+'. Review assumptions in the JSON.';}finally{button.disabled=false;}
});
$('import-file').onchange=guarded(async()=>{
  const file=$('import-file').files[0];if(!file)return;
  const text=await file.text();
  const definition=file.name.toLowerCase().endsWith('.dbc')?(await api('/api/dbc',{text})).definition:JSON.parse(text);
  editDefinition(definition);toast('Definition loaded for review.');$('import-file').value='';
});
$('download-definition').onclick=guarded(async()=>{
  const definition=JSON.parse($('definition').value),blob=new Blob([JSON.stringify(definition,null,2)],{type:'application/json'});
  const url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download=definition.name.replace(/[^a-z0-9_-]/gi,'_')+'.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
});
window.addEventListener('resize',()=>state&&render());
async function poll() {
  try{state=await api('/api/state');render();$('connection-label').textContent='Runtime connected';$('connection-light').style.background='var(--cyan)';}
  catch(error){$('connection-label').textContent='Runtime unavailable';$('connection-light').style.background='var(--red)';}
}
(async()=>{
  await poll();
  try{
    templates=await api('/api/templates');$('template-list').replaceChildren(...Object.entries(templates).map(([name,definition])=>{
      const button=document.createElement('button');button.textContent=definition.name;button.onclick=()=>editDefinition(definition);return button;
    }));editDefinition(templates.abs);
  }catch(error){toast(error.message,true);}
  setInterval(poll,250);
})();
