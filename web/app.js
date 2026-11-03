'use strict';
const $ = id => document.getElementById(id);
const escape = value => String(value).replace(/[&<>"']/g, char => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[char]));
let state, templates={}, selected='Motor ECU', activeTab='simulate', frameSelection, history=[], lastTime=-1, dragging=false, dragNode=null;
const positions=new Map();let topologyShape='';let mutationVersion=0,commandQueue=Promise.resolve();
async function api(url,body) {
  if(body)mutationVersion++;
  const result=await fetch(url,body?{method:'POST',headers:{'Content-Type':'application/json'},body:typeof body==='string'?body:JSON.stringify(body)}:{});
  const data=await result.json();
  if(!result.ok || data.error)throw new Error(data.error || 'Request failed');
  return data;
}
function toast(message,error=false) {
  $('toast').textContent=message;$('toast').className='toast show'+(error?' error':'');
  clearTimeout(toast.timer);toast.timer=setTimeout(()=>$('toast').classList.remove('show'),5000);
}
function command(action,extra={}) {
  const task=commandQueue.catch(()=>{}).then(async()=>{
    const data=await api('/api/command',{action,...extra});
    if(data.ecus){state=data;render();}
    return data;
  });commandQueue=task;return task;
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
  const connected=state.cluster?.connected||0,required=state.cluster?.required||0;
  $('cluster-mode').textContent=state.cluster?.controller==='native-cpp'?'Native C++ engine':'Compatibility runtime';
  $('node-count').textContent=required?connected+' / '+required:String(state.ecus.length);
  $('cluster-status').textContent=state.cluster?.last_error?'Clock paused · worker fault':required?'All required workers connected':'Local kernel runtime';
  $('container-summary').textContent=required?'1 controller + '+required+' ECUs + '+(state.cluster.reserve_slots||0)+' reserve':'Use Docker Compose for separate ECU containers';
  $('scenario-name').textContent=state.name;
  if(state.project_hash)$('project-hash').textContent=state.project_hash;
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
  $('bus-load').textContent=state.buses?.map(b=>b.name.replace('Powertrain ','').replace('ADAS ','')+' '+number(b.load_percent,1)+'%').join(' · ')||'Virtual bus active';
  for(const [id,key] of [['assist-enabled','assist_enabled'],['aeb-enabled','aeb_enabled']])$(id).checked=Boolean(state.controls[key]);
  for(const [id,key] of [['target-speed','target_speed'],['lead-speed','lead_speed']])if(document.activeElement!==$(id))$(id).value=state.controls[key];
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
  const keys=Object.keys(ecu.definition.variables).slice(0,8);
  $('inspector-values').innerHTML=valuesHTML(Object.fromEntries(keys.map(k=>[k,ecu.values[k]])));
  const hardware=ecu.hardware||{};
  $('hardware-values').innerHTML='<div><small>VIRTUAL CPU</small><strong>'+number(hardware.cpu_mhz||ecu.definition.hardware?.cpu_mhz||120,0)+' MHz</strong></div><div><small>TASK LOAD</small><strong>'+number(hardware.cpu_load_percent,1)+'%</strong></div><div><small>VIRTUAL RAM</small><strong>'+number((hardware.ram_capacity_bytes||131072)/1024,0)+' KiB</strong></div><div><small>WATCHDOG</small><strong>'+(ecu.container?.status==='fault'?'TRIPPED':'HEALTHY')+'</strong></div>';
  $('kernel-health').innerHTML='<div class="kernel-status'+(ecu.container?.status==='fault'?' fault':'')+'"><i></i>'+escape(ecu.container?.service||'embedded')+' · '+escape(ecu.container?.status||'local')+'</div>';
  $('halt-node').disabled=state.cluster?.mode!=='distributed';$('restore-node').disabled=state.cluster?.mode!=='distributed';
  $('inspector-json').textContent=JSON.stringify(ecu.definition,null,2);
  if(activeTab==='simulate'){renderNodes();renderScene();drawChart();}
  if(activeTab==='network')renderNetwork();
  if(activeTab==='analyze')renderFrames();
  if(activeTab==='diagnose')renderDTCs();
  if(activeTab==='performance')renderPerformance();
  if(window.renderNative)window.renderNative(state);
}
function renderNodes() {
  const holder=$('ecu-nodes'),area=$('topology'),width=area.clientWidth,narrow=width<540;
  const top=state.ecus.filter(e=>['Motor ECU','ABS ECU','BMS ECU','Dynamics ECU'].includes(e.name));
  const bottom=state.ecus.filter(e=>!top.includes(e));
  const shape=state.ecus.map(e=>e.name).join('|')+':'+narrow;
  if(shape!==topologyShape){positions.clear();topologyShape=shape;}
  const maxCols=narrow?2:5,topBase=narrow?18:35,powerY=topBase+Math.ceil(top.length/Math.min(maxCols,top.length))*123+10,bottomBase=powerY+55,fdY=bottomBase+Math.ceil(bottom.length/Math.min(maxCols,bottom.length))*123+10;
  area.style.height=Math.max(440,fdY+50)+'px';$('powertrain-bus').style.top=powerY+'px';$('adas-bus').style.top=fdY+'px';const height=area.clientHeight;
  for(const ecu of state.ecus){
    const key=ecu.name;
    let node=[...holder.children].find(n=>n.dataset.name===key);
    if(!node){node=document.createElement('div');node.className='ecu-node';node.dataset.name=key;holder.append(node);node.onpointerdown=startDrag;node.onclick=()=>{if(dragging)return;selected=key;render();};}
    const list=top.includes(ecu)?top:bottom,index=list.indexOf(ecu),cols=Math.min(maxCols,list.length);
    if(!positions.has(key)){
      const y=(top.includes(ecu)?topBase:bottomBase)+Math.floor(index/cols)*123;
      positions.set(key,{x:(index%cols+.5)/cols,y:y/height});
    }
    const fault=ecu.state==='FAULT'||ecu.container?.status==='fault';
    node.className='ecu-node'+(key===selected?' selected':'')+(fault?' fault':'');
    const tx=ecu.definition.messages.filter(m=>m.direction==='tx');
    const protocol=tx.some(m=>m.fd)?'CAN FD':'CAN';
    const icon=key.includes('Motor')?'⚙':key.includes('ABS')?'◉':key.includes('BMS')?'▰':key.includes('Radar')?'⌁':key.includes('ADAS')?'◈':key.includes('Dynamics')?'↗':'▤';
    const v=ecu.values;
    const display=key.includes('Motor')?number(v.motor_rpm,0)+' rpm · '+number(v.motor_temperature)+' °C':
      key.includes('ABS')?(v.abs_active?'ABS ACTIVE':'READY')+' · '+number(v.pressure*100,0)+'%':
      key.includes('BMS')?number(v.battery_soc)+'% · '+number(v.pack_voltage,0)+' V':
      key.includes('Radar')?number(v.obj0_range)+' m · '+number(v.obj0_closing_speed)+' m/s · '+number(v.object_count)+' obj':
      key.includes('ADAS')?'TTC '+number(v.time_to_collision)+' s · AEB '+(v.aeb_request?'ON':'OFF')+(v.perception_valid===0?' · FUSION HOLD':''):
      key.includes('Dynamics')?number(v.vehicle_speed)+' km/h':number(v.display_speed)+' km/h · '+number(v.display_soc)+'%';
    node.innerHTML='<div class="node-head"><span>'+icon+'</span><span class="node-status">'+escape(ecu.state)+'</span></div><div class="node-name">'+escape(key)+'</div><div class="node-value">'+escape(display)+'</div><div class="node-can">'+escape(protocol+' · '+(tx.map(m=>'0x'+m.id.toString(16).toUpperCase()).join(' / ')||'receive'))+'</div><div class="node-service">'+escape(ecu.container?.service||'local kernel')+' · '+number(ecu.hardware?.cpu_load_percent||0,1)+'% CPU</div>';
    const p=positions.get(key);
    if(dragNode!==node){node.style.left=Math.max(0,Math.min(width-node.offsetWidth,width*p.x-node.offsetWidth/2))+'px';node.style.top=Math.max(0,Math.min(height-node.offsetHeight,height*p.y))+'px';}
  }
  for(const node of [...holder.children])if(!state.ecus.some(e=>e.name===node.dataset.name))node.remove();
  let lines='';
  for(const node of holder.children){
    const ecu=state.ecus.find(e=>e.name===node.dataset.name),buses=new Set(ecu.definition.messages.map(m=>m.bus||'powertrain'));
    for(const bus of buses){
      const bar=$(bus==='adas'?'adas-bus':'powertrain-bus'),x=node.offsetLeft+node.offsetWidth/2,y=node.offsetTop;
      const below=y>bar.offsetTop,anchor=below?y:y+node.offsetHeight,target=bar.offsetTop+(below?bar.offsetHeight:0);
      lines+='<path d="M'+x+' '+anchor+' L'+x+' '+target+'" stroke="'+(bus==='adas'?'#9caf89':'#a5c585')+'" stroke-width="1.4" fill="none"/><circle cx="'+x+'" cy="'+target+'" r="2.6" fill="#80a953"/>';
    }
  }
  $('topology-lines').innerHTML=lines;
}
function renderScene(){
  const adas=state.ecus.find(e=>e.name==='ADAS ECU'),v=adas?.values||{},gap=state.world.lead_distance||0;
  $('adas-state').textContent=adas?.state||'MONITORING';$('lead-gap').textContent=number(gap)+' m';
  $('ttc').textContent=v.time_to_collision>=99?'—':number(v.time_to_collision,2)+' s';
  $('fusion-status').textContent=v.perception_valid===0?'DISAGREE ('+number(v.perception_gap,1)+' m)':'OK ('+number(v.perception_gap,1)+' m)';
  $('fusion-status').style.color=v.perception_valid===0?'var(--red)':'var(--green)';
  $('aeb-status').textContent=v.aeb_request?'BRAKING':'READY';$('aeb-status').style.color=v.aeb_request?'var(--red)':'var(--green)';
  $('acc-status').textContent=v.acc_active?'ACTIVE':'OFF';
  const leadX=Math.min(640,230+gap*2),offset=(state.world.position*10)%80,danger=v.aeb_request||state.world.collision;
  const car=(x,green)=>'<g transform="translate('+x+',58)"><rect width="72" height="37" rx="11" fill="'+(green?'#92cc48':'#d6dfce')+'" stroke="'+(green?'#5e9a29':'#a7b499')+'"/><rect x="18" y="5" width="33" height="27" rx="7" fill="'+(green?'#dff1c7':'#f6f9f3')+'"/><rect x="29" y="8" width="12" height="21" rx="2" fill="'+(green?'#70a437':'#b7c4a9')+'"/><path d="M4 7h8M4 30h8M58 7h8M58 30h8" stroke="#4c6140" stroke-width="3"/></g>';
  $('vehicle-scene').innerHTML='<svg viewBox="0 0 800 155" role="img" aria-label="Synthetic road scene with ego vehicle, radar field, and lead vehicle"><defs><pattern id="lanes" width="80" height="1" patternUnits="userSpaceOnUse" x="'+(-offset)+'"><path d="M0 0H40" stroke="#bec9b2" stroke-width="2"/></pattern></defs><rect width="800" height="155" fill="#f6f9f1"/><rect y="26" width="800" height="101" fill="#eaf0e3"/><path d="M0 27H800M0 127H800" stroke="#cfdbc4"/><rect y="47" width="800" height="2" fill="url(#lanes)"/><rect y="105" width="800" height="2" fill="url(#lanes)"/><path d="M173 76L'+leadX+' 47L'+leadX+' 105Z" fill="'+(danger?'#eea09c':'#afd572')+'" opacity=".18"/>'+car(100,true)+car(leadX,false)+'<path d="M176 119H'+leadX+'" stroke="'+(danger?'#cb655a':'#9fb683')+'" stroke-dasharray="3 4"/><text x="100" y="145" fill="#6f8f56" font-size="9" font-family="monospace">EGO VEHICLE</text><text x="'+leadX+'" y="145" fill="#8c9b80" font-size="9" font-family="monospace">LEAD VEHICLE</text><text x="105" y="18" fill="#8a9e7b" font-size="8" font-family="monospace">SYNTHETIC RADAR FIELD · '+number(state.controls.lead_speed*3.6,0)+' km/h LEAD SPEED</text></svg>';
}
function renderNetwork(){
  $('bus-cards').innerHTML=(state.buses||[]).map(bus=>'<div class="bus-card"><span class="eyebrow">'+escape(bus.name.toUpperCase())+'</span><h2>'+number(bus.bitrate/1000,0)+' kbps'+(bus.fd?' / '+number(bus.data_bitrate/1000000,0)+' Mbps':'')+'</h2><p>'+ (bus.fd?'CAN FD · up to 64 bytes · bitrate switching':'Classic CAN · 0–8 byte payloads')+'</p><strong>'+number(bus.load_percent,2)+'%</strong><small>estimated bus load</small><div class="meter"><i style="width:'+Math.min(100,bus.load_percent)+'%"></i></div><p>Backlog '+number(bus.backlog_us,0)+' μs · serialization estimated, not bit-exact</p></div>').join('');
  setOptions('policy-source',[['','All transmitters'],...state.ecus.map(e=>[e.name,e.name]),['Diagnostic tester','Diagnostic tester']]);
  $('registry').innerHTML=state.ecus.map(ecu=>'<div class="registry-row"><div>'+escape(ecu.name)+'<small>'+escape(ecu.container?.service||'embedded')+' · virtual-ecu-kernel</small></div><span>'+escape(ecu.container?.status||'local')+'</span></div>').join('');
  $('policy-list').innerHTML=state.policies.map(rule=>'<div class="policy-row">'+escape(rule.source||'ALL ECUs')+' · '+escape(rule.bus||'ALL buses')+(rule.id!=null?' · 0x'+rule.id.toString(16):'')+'<br>'+(rule.drop?'DROP':'DELIVER')+' · '+number(rule.latency_ms||0,0)+' ms extra latency</div>').join('')||'<p>No flow rules. Default broadcast delivery with declared gateway routes.</p>';
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
  ctx.font='9px monospace';ctx.fillStyle='#8a9e7a';ctx.strokeStyle='#e5eddb';ctx.lineWidth=1;
  for(let i=0;i<5;i++){const y=top+(bottom-top)*i/4;ctx.beginPath();ctx.moveTo(left,y);ctx.lineTo(right,y);ctx.stroke();ctx.fillText(String(200-i*50),0,y+3);}
  const end=Math.max(30,state?.time||0),start=end-30;
  for(let i=0;i<=5;i++){const x=left+(right-left)*i/5;ctx.fillText(number(start+i*6,0)+'s',x,bottom+14);}
  for(const [column,max,color] of [[1,200,'#75a640'],[2,160,'#697866']]){
    ctx.strokeStyle=color;ctx.lineWidth=2;ctx.beginPath();
    history.forEach((point,index)=>{const x=left+(point[0]-start)/30*(right-left),y=bottom-Math.min(1,Math.max(0,point[column]/max))*(bottom-top);index?ctx.lineTo(x,y):ctx.moveTo(x,y);});ctx.stroke();
  }
}
function renderFrames() {
  const filter=$('frame-filter').value.toLowerCase();
  const frames=state.frames.slice().reverse().filter(f=>JSON.stringify({...f,protocol:f.fd?'CAN FD':'CAN',hex:'0x'+f.id.toString(16)}).toLowerCase().includes(filter));
  $('frame-counter').textContent=state.frame_count+' total frames · '+frames.length+' shown';
  $('frames').innerHTML=frames.map((f,index)=>'<tr data-index="'+index+'"><td>'+number(f.time,4)+'</td><td>'+escape(f.bus||'powertrain')+'</td><td><span class="protocol'+(f.fd?' fd':'')+'">'+(f.fd?'CAN FD'+(f.brs?' · BRS':''):'CAN')+'</span></td><td>0x'+f.id.toString(16).toUpperCase()+'</td><td>'+escape(f.ecu)+'</td><td>'+escape(f.name)+(f.dropped?' · DROPPED':f.gateway?' · gateway':'')+'</td><td>'+f.dlc+'</td><td>'+escape(f.bytes)+'</td></tr>').join('');
  $('frames').querySelectorAll('tr').forEach(row=>row.onclick=()=>{frameSelection=frames[Number(row.dataset.index)];renderDecoded();});
  $('events').innerHTML=state.events.slice().reverse().map(e=>'<div><small>'+number(e.time,2)+' s · '+escape(e.ecu)+'</small>'+escape(e.from)+' → '+escape(e.to)+'</div>').join('')||'<p>Run or step the simulation to see transitions.</p>';
}
function renderDecoded(){const f=frameSelection;$('decoded-frame').innerHTML='<h2>0x'+f.id.toString(16).toUpperCase()+' · '+escape(f.name)+'</h2><div class="value-list">'+valuesHTML(f.signals)+'</div>';}
function renderDTCs() {
  const codes=state.ecus.flatMap(e=>e.dtcs.map(d=>({...d,ecu:e.name})));
  $('dtcs').innerHTML=codes.map(d=>'<div class="dtc'+(!d.active?' recovered':'')+'"><div class="dtc-header"><strong>'+escape(d.code)+'</strong><span class="chip">'+(d.active?'ACTIVE':'STORED / RECOVERED')+'</span></div><h2>'+escape(d.name)+'</h2><p>'+escape(d.ecu)+' · '+escape(d.severity)+'</p></div>').join('')||'<div class="empty">No diagnostic trouble codes.<br>Inject a sensor fault to test the detection chain.</div>';
}
const micros=value=>number((value||0)/1000,2)+' ms';
function renderPerformance() {
  const perf=state.performance||{},totals=perf.totals||{},ecus=perf.ecus||[],buses=perf.buses||[];
  $('perf-window').textContent=number(totals.window_s||0,1)+' s window · '+number(totals.ticks||0,0)+' ticks';
  const kpis=[
    ['E2E CONTROL LATENCY',micros(totals.chain_p95_us),'sampled → applied · barrier '+number(totals.step_ms||10,0)+' ms'],
    ['TASK COST P95',micros(totals.task_p95_us),'worst ECU computation'],
    ['DELIVERY P95',micros(totals.delivery_p95_us),'computed → delivered on the bus'],
    ['THROUGHPUT',number(totals.frame_rate_hz||0,0)+' f/s',number(totals.dropped||0,0)+' dropped · '+number(totals.frames||0,0)+' frames']
  ];
  $('perf-kpis').innerHTML=kpis.map(([label,value,note])=>'<div class="bus-card"><span class="eyebrow">'+escape(label)+'</span><h2>'+escape(value)+'</h2><strong>'+escape(value)+'</strong><small>'+escape(note)+'</small></div>').join('');
  const worst=Math.max(1,...ecus.map(e=>e.task.p95||0));
  $('perf-ecus').innerHTML=ecus.map(e=>'<tr><td>'+escape(e.name)+'<div class="meter"><i style="width:'+Math.min(100,100*(e.task.p95||0)/worst)+'%"></i></div></td>'
    +'<td>'+micros(e.task.p50)+'</td><td>'+micros(e.task.p95)+'</td><td>'+micros(e.task.max)+'</td>'
    +'<td>'+micros(e.delivery.p95)+'</td><td>'+(e.e2e.count?micros(e.e2e.p95):'—')+'</td>'
    +'<td>'+number(e.tx||0,0)+'</td><td>'+number(e.rx||0,0)+'</td><td>'+number(e.dropped||0,0)+'</td><td>'+number(e.states||0,0)+'</td></tr>').join('')
    ||'<tr><td colspan="10">Run the simulation to collect latency samples.</td></tr>';
  $('perf-buses').innerHTML=buses.map(b=>'<div class="registry-row"><div>'+escape(b.name||b.id)+'<small>'+number(b.frame_rate_hz||0,0)+' frame/s · backlog '+number(b.backlog_us||0,0)+' µs</small></div>'
    +'<span>'+number(b.load_percent||0,2)+'%</span></div><div class="meter'+(b.load_percent>70?' graphite':'')+'"><i style="width:'+Math.min(100,b.load_percent||0)+'%"></i></div>'
    +'<p class="tip">'+number(b.frames||0,0)+' frames · '+number(b.dropped||0,0)+' dropped</p>').join('')||'<p>No bus defined.</p>';
  setOptions('perf-ecu',ecus.map(e=>[e.name,e.name]));
  drawPerformanceChart();
}
function drawPerformanceChart() {
  const perf=state.performance||{},ecus=perf.ecus||[],box=$('perf-chart');
  if(!box||!ecus.length)return;
  const ecu=ecus.find(e=>e.name===$('perf-ecu').value)||ecus[0];
  const end=state.time||0,start=Math.max(0,end-30);
  const task=[],delivery=[];
  for(const sample of ecu.timeline||[]) {
    const t=(sample.t_us||0)/1e6;if(t<start||t>end+1e-6)continue;
    if(sample.task_us!=null)task.push([t,sample.task_us]);
    if(sample.delivery_us!=null)delivery.push([t,sample.delivery_us]);
  }
  const values=[...task,...delivery].map(p=>p[1]);
  const top=Math.max(1000,...values)*1.15;
  const w=box.clientWidth||760,h=210,left=52,right=w-14,top_=14,bottom=h-26;
  const x=t=>left+(t-start)/30*(right-left),y=v=>bottom-Math.min(1,v/top)*(bottom-top_);
  const path=points=>points.length?points.map((p,i)=>(i?'L':'M')+x(p[0]).toFixed(1)+' '+y(p[1]).toFixed(1)).join(' '):'';
  let grid='';
  for(let i=0;i<=4;i++){const value=top*i/4,gy=y(value);grid+='<line x1="'+left+'" y1="'+gy.toFixed(1)+'" x2="'+right+'" y2="'+gy.toFixed(1)+'" stroke="#e6eedd"/>'
    +'<text x="'+(left-8)+'" y="'+(gy+3).toFixed(1)+'" text-anchor="end" font-size="9" font-family="monospace" fill="#8a9e7a">'+number(value/1000,1)+'</text>';}
  for(let i=0;i<=6;i++){const t=start+i*5,gx=x(t);grid+='<text x="'+gx.toFixed(1)+'" y="'+(bottom+16)+'" text-anchor="middle" font-size="9" font-family="monospace" fill="#8a9e7a">'+number(t,0)+'s</text>';}
  const empty=!task.length&&!delivery.length;
  box.innerHTML='<svg viewBox="0 0 '+w+' '+h+'" role="img" aria-label="Task cost and delivery latency timeline">'+grid
    +'<path d="'+path(task)+'" fill="none" stroke="#75a640" stroke-width="2"/>'
    +'<path d="'+path(delivery)+'" fill="none" stroke="#697866" stroke-width="1.6" stroke-dasharray="4 3"/>'
    +(empty?'<text x="'+(w/2)+'" y="'+(h/2)+'" text-anchor="middle" font-size="11" font-family="monospace" fill="#8a9e7a">NO SAMPLES YET — RUN THE SIMULATION</text>':'')
    +'<text x="'+left+'" y="'+(bottom+16)+'" font-size="9" font-family="monospace" fill="#8a9e7a"></text></svg>'
    +'<div class="legend"><span class="legend-dot"></span>task cost '+escape(ecu.name)+' · <span class="legend-dot graphite"></span>bus delivery latency · scale in ms</div>';
}
$('play').onclick=guarded(()=>command(state.running?'pause':'run'));
$('step').onclick=guarded(async()=>{await command('pause');await command('step');});
$('reset').onclick=guarded(async()=>{await command('restart_project');frameSelection=null;toast('Scenario reset.');});
let controlTimer;
for(const [id,key] of [['throttle','throttle'],['brake','brake'],['friction','road_friction']]){
  $(id).oninput=()=>{const value=Number($(id).value)/100;$(id+'-value').textContent=id==='friction'?number(value,2)+' μ':Math.round(value*100)+'%';clearTimeout(controlTimer);controlTimer=setTimeout(guarded(()=>command('controls',{values:{throttle:Number($('throttle').value)/100,brake:Number($('brake').value)/100,road_friction:Number($('friction').value)/100}})),40);};
}
$('ignition').onchange=guarded(()=>command('controls',{values:{ignition:$('ignition').checked?1:0}}));
$('inject').onclick=guarded(async()=>{await command('fault',{sensor:$('fault-sensor').value,mode:$('fault-mode').value,value:Number($('fault-value').value)});toast('Fault injected. Run or step to observe the response.');});
$('clear-fault').onclick=guarded(async()=>{await command('fault',{sensor:$('fault-sensor').value,mode:'none'});toast('Sensor restored. Stored DTCs remain until cleared.');});
$('abs-demo').onclick=guarded(async()=>{
  await command('reset');await command('controls',{values:{throttle:.7}});await command('environment',{values:{speed:25,lead_distance:150}});
  await command('controls',{values:{throttle:0,brake:1,road_friction:.2}});await command('run');toast('Low-friction braking active. Inspect the ABS ECU and BrakeStatus frames.');
});
$('frame-filter').oninput=()=>state&&renderFrames();
$('perf-ecu').onchange=()=>state&&drawPerformanceChart();
$('uds-preset').onchange=()=>$('uds-bytes').value=$('uds-preset').value;
$('send-uds').onclick=guarded(async()=>{
  const tokens=$('uds-bytes').value.trim().split(/\s+/);
  if(!tokens.every(t=>/^[0-9a-fA-F]{2}$/.test(t)))throw new Error('Use two-digit hex bytes separated by spaces.');
  const response=await command('diagnostic',{ecu:$('diagnostic-ecu').value,bytes:tokens.map(t=>parseInt(t,16)),fd:$('uds-protocol').value==='fd'});
  $('uds-result').textContent='TX 0x7E0   '+response.request+'\nRX 0x7E8   '+response.response+'\n\n'+response.transport+' · '+(response.frame_count||'')+' frames';
  await poll();
});
function editDefinition(definition){$('definition').value=JSON.stringify(definition,null,2);$('validation-badge').textContent='UNVALIDATED';}
$('definition').oninput=()=>$('validation-badge').textContent='UNVALIDATED';
$('validate').onclick=guarded(async()=>{await command('validate',{definition:JSON.parse($('definition').value)});$('validation-badge').textContent='VALID';$('validation-result').textContent='Encoding, timing, state machine, and expressions passed. Adding checks the full network.';toast('Definition is valid.');});
$('add-ecu').onclick=guarded(async()=>{await command('add_ecu',{definition:JSON.parse($('definition').value)});toast('ECU assigned to a dedicated kernel. Download JSON to preserve it.');tab('simulate');});
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
$('export-project').onclick=guarded(async()=>{
  const response=await fetch('/api/project');if(!response.ok)throw Error('Project export failed');
  const text=await response.text(),project=JSON.parse(text);
  const blob=new Blob([text],{type:'application/json'});
  const url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download=(project.name||'project').replace(/[^a-z0-9_-]/gi,'_')+'.vcanproj';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
  $('project-badge').textContent='EXPORTED';$('project-hash').textContent=project.project_hash;
  toast('Project exported · '+project.project_hash.slice(7,19)+'…');
});
$('import-project').onchange=guarded(async()=>{
  const file=$('import-project').files[0];if(!file)return;
  const text=await file.text();JSON.parse(text);
  const result=await api('/api/project','{"project":'+text+'}');
  $('project-badge').textContent='IMPORTED';$('project-hash').textContent=result.project_hash;
  toast('Project imported · '+result.project_hash.slice(7,19)+'…');
  await poll();
});
$('assist-enabled').onchange=guarded(()=>command('controls',{values:{assist_enabled:$('assist-enabled').checked?1:0}}));
$('aeb-enabled').onchange=guarded(()=>command('controls',{values:{aeb_enabled:$('aeb-enabled').checked?1:0}}));
$('target-speed').onchange=guarded(()=>command('controls',{values:{target_speed:Number($('target-speed').value)}}));
$('lead-speed').onchange=guarded(()=>command('controls',{values:{lead_speed:Number($('lead-speed').value)}}));
$('aeb-demo').onclick=guarded(async()=>{
  await command('reset');await command('environment',{values:{speed:25,lead_distance:45}});
  await command('controls',{values:{throttle:0,lead_speed:0,road_friction:.95,aeb_enabled:1}});
  await command('run');toast('Emergency braking scenario: 90 km/h, stopped lead vehicle, high-friction road.');
});
$('halt-node').onclick=guarded(async()=>{await command('node_fault',{name:selected,mode:'halt'});toast('Kernel halted. Simulation paused; other containers remain alive.');});
$('restore-node').onclick=guarded(async()=>{await command('node_fault',{name:selected,mode:'restore'});toast('Kernel restored. Run to resume or reset the scenario.');});
$('apply-policy').onclick=guarded(async()=>{
  const rule={latency_ms:Number($('policy-latency').value),drop:$('policy-drop').checked};
  if($('policy-source').value)rule.source=$('policy-source').value;
  if($('policy-bus').value)rule.bus=$('policy-bus').value;
  if($('policy-id').value.trim()){if(!/^(0x)?[0-9a-f]+$/i.test($('policy-id').value.trim()))throw Error('CAN ID must be hexadecimal');rule.id=parseInt($('policy-id').value,16);}
  await command('network_policy',{rules:[...state.policies,rule]});toast('Flow rule applied to the virtual bus.');
});
$('clear-policies').onclick=guarded(async()=>{await command('network_policy',{rules:[]});toast('All flow rules cleared.');});
window.addEventListener('resize',()=>{positions.clear();if(state)render();});
let polling=false;
async function poll() {
  if(polling)return;polling=true;
  try{const version=mutationVersion,data=await api('/api/state');if(version===mutationVersion){state=data;render();}$('connection-label').textContent='Runtime connected';$('connection-light').style.background='var(--cyan)';}
  catch(error){$('connection-label').textContent='Runtime unavailable';$('connection-light').style.background='var(--red)';}
  finally{polling=false;}
}
(async()=>{
  await poll();
  try{
    templates=await api('/api/templates');$('template-list').replaceChildren(...Object.entries(templates).map(([name,definition])=>{
      const button=document.createElement('button');button.textContent=definition.name;button.onclick=()=>editDefinition(definition);return button;
    }));editDefinition(templates.abs);
  }catch(error){toast(error.message,true);}
  setInterval(poll,150);
})();
