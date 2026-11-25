'use strict';
const nativeSessions=new Map();let nativeSlot='session:1',nativeNextSlot=2,nativeActorId=1,nativeEpoch,nativeMesh=null,nativeMeshKey='',nativeDefaultMesh=null,nativeLoading=false;
let followingDraft=false,environmentDraft=false,physicsDraft=false,nativeOrbit=-.55,nativeDistance=9;
const nval=id=>Number($(id).value);
for(const id of ['native-mass','native-ratio','native-brakes','native-torque'])$(id).addEventListener('input',()=>physicsDraft=true);
for(const id of ['native-timegap','native-standstill','native-gain'])$(id).addEventListener('input',()=>followingDraft=true);
for(const id of ['native-grade','native-wind'])$(id).addEventListener('input',()=>environmentDraft=true);
function nativeFill(id,value){if(document.activeElement!==$(id))$(id).value=value;}
window.renderNative=s=>{
  const w=s.world,c=s.controls,restarted=nativeEpoch!==s.epoch;nativeEpoch=s.epoch;
  if(restarted){followingDraft=false;environmentDraft=false;physicsDraft=false;}
  $('native-authority').textContent=`C++ clock · ${s.cluster.connected}/${s.cluster.required} worker processes · tick ${s.tick}`;
  $('container-summary').textContent=`1 C++ controller + ${s.cluster.required} isolated native ECU workers`;
  nativeFill('native-gear',c.gear??0);nativeFill('native-steer',Math.round((c.steering??0)*100));
  if(!followingDraft){nativeFill('native-timegap',c.following_time_s??1.5);nativeFill('native-standstill',c.following_gap_m??7);nativeFill('native-gain',c.following_gain??.45);}
  if(!environmentDraft){nativeFill('native-grade',c.road_grade_deg??0);nativeFill('native-wind',c.headwind_mps??0);}
  $('lead-speed').disabled=Boolean(s.traffic);$('lead-speed').title=s.traffic?'Use traffic actor controls below':'';
  const actors=s.traffic?.actors??[];setOptions('native-actor',actors.map(a=>[a.id,`${a.id} · ${(a.speed*3.6).toFixed(1)} km/h`]));
  const adas=s.ecus.find(e=>e.name==='ADAS ECU')?.values??{};
  $('native-follow').disabled=!('following_target_kph' in adas)||!('following_time_s' in c);
  $('native-following').textContent=`Lead ${s.traffic?.lead_id||'none'} · gap ${number(w.lead_distance)} m · closing ${number(w.closing_speed)} m/s · ECU target ${number(adas.following_target_kph)} km/h · brake ${number((adas.following_brake??adas.aeb_request??0)*100)}%`;
  $('native-environment').disabled=s.physics?.model!=='bicycle_grip_v2';$('native-physics').disabled=!s.physics;
  if(!physicsDraft){for(const [id,key,value] of [['native-mass','mass_kg',1500],['native-ratio','final_drive_ratio',9],['native-brakes','brake_force_n',12000],['native-torque','motor_torque_limit_nm',320]])nativeFill(id,s.physics?.[key]??value);}
  if(activeTab==='signals')$('native-signals').textContent=JSON.stringify({world:w,controls:c,sensors:s.sensors,ecus:s.ecus.map(e=>({name:e.name,state:e.state,values:e.values,hardware:e.hardware,resources:e.resources,container:e.container}))},null,2);
  const faults=s.sensors.filter(x=>x.fault!=='none').length,dtcs=s.ecus.flatMap(e=>e.dtcs.filter(d=>d.active)).length,alerts=[];
  if(w.motor_temperature>=100)alerts.push('MOTOR HOT');if(w.battery_temperature>=50)alerts.push('BATTERY HOT');if(w.soc<15)alerts.push('LOW CHARGE');if(faults)alerts.push(`${faults} sensor faults`);if(dtcs)alerts.push(`${dtcs} DTCs`);if(s.cluster.connected!==s.cluster.required)alerts.push('WORKER FAULT');if(w.collision)alerts.push('COLLISION');if((w.tire_utilization??0)>.9)alerts.push('GRIP LIMIT');if(w.collision_events)alerts.push(`${w.collision_events} contacts in run`);
  $('native-health').textContent=alerts.join(' · ')||'HEALTHY · live native measurements';
  $('health-motor').setAttribute('fill',w.motor_temperature>=100?'#ed9a78':'#b3e765');$('health-battery').setAttribute('fill',w.soc<15||w.battery_temperature>=50?'#ed9a78':'#b3e765');$('health-tires').setAttribute('fill',(w.tire_utilization??0)>.9?'#ed9a78':'#70bf90');
  $('health-motor-text').textContent=`${number(w.motor_temperature,0)} °C`;$('health-battery-text').textContent=`${number(w.soc,0)}% · ${number(w.battery_temperature,0)}°C`;$('health-use').textContent=`Grip ${number((w.tire_utilization??0)*100,0)}% · brake ${number(w.brake_pressure*100,0)}%`;
  const key=JSON.stringify(s.presentation?.car_mesh??null)+':'+s.epoch;
  if(key!==nativeMeshKey&&!nativeLoading){nativeLoading=true;api('/api/car-mesh').then(mesh=>{nativeMesh=mesh;nativeMeshKey=key;if(!s.presentation?.car_mesh)nativeDefaultMesh=mesh;drawNativeScene();}).catch(error=>toast(error.message,true)).finally(()=>nativeLoading=false);}
  drawNativeScene();
};
$('native-gear').onchange=guarded(()=>command('controls',{values:{gear:nval('native-gear')}}));
$('native-steer').onchange=guarded(()=>command('controls',{values:{steering:nval('native-steer')/100}}));
$('native-follow').onclick=guarded(async()=>{await command('controls',{values:{assist_enabled:1,aeb_enabled:1,target_speed:nval('target-speed'),following_time_s:nval('native-timegap'),following_gap_m:nval('native-standstill'),following_gain:nval('native-gain')}});followingDraft=false;window.renderNative(state);toast('Following applied through native ADAS/CAN. Select D and Run.');});
$('native-environment').onclick=guarded(async()=>{await command('controls',{values:{road_grade_deg:nval('native-grade'),headwind_mps:nval('native-wind')}});environmentDraft=false;window.renderNative(state);});
$('native-stress').onclick=guarded(()=>command('environment',{values:{motor_temperature:nval('native-temp'),soc:nval('native-soc')}}));
async function nativeActor(operation,reposition){
  let id=$('native-actor').value,actor={target_speed:nval('native-actor-speed')/3.6};
  if(operation==='add'){do{id=`web-car-${nativeActorId++}`;}while(state.traffic?.actors.some(a=>a.id===id));actor.speed=actor.target_speed;}
  if(!id)throw Error('Select or add a traffic car first.');
  if(reposition){const w=state.world,h=w.heading??0,gap=nval('native-gap'),side=nval('native-offset');Object.assign(actor,{x:(w.x??w.position)+gap*Math.cos(h)-side*Math.sin(h),y:(w.y??0)+gap*Math.sin(h)+side*Math.cos(h),yaw:h});}
  await command('traffic_actor',{operation,id,actor});$('native-actor').value=id;
}
$('native-add').onclick=guarded(()=>nativeActor('add',true));$('native-speed').onclick=guarded(()=>nativeActor('update',false));$('native-move').onclick=guarded(()=>nativeActor('update',true));$('native-remove').onclick=guarded(()=>nativeActor('remove',false));
$('native-switch').onclick=guarded(async()=>{
  $('native-switch').disabled=true;$('native-project').disabled=true;
  try{const chosen=$('native-project').value;await command('pause');const exported=await fetch('/api/project');if(!exported.ok)throw Error('Cannot preserve current project');nativeSessions.set(nativeSlot,await exported.text());
  if(chosen.startsWith('session:')){const project=nativeSessions.get(chosen);if(!project)throw Error('Session is unavailable');await api('/api/project','{"project":'+project+'}');nativeSlot=chosen;}
  else{await command('new_project',{scenario:chosen});nativeSlot=`session:${nativeNextSlot++}`;}
  await poll();const picker=$('native-project');if(![...picker.options].some(o=>o.value===nativeSlot)){const option=document.createElement('option');option.value=nativeSlot;option.textContent=`${state.name} · ${nativeSlot}`;picker.add(option);}picker.value=nativeSlot;toast('Project switched at tick zero; saved settings retained in this session.');
  }finally{$('native-switch').disabled=false;$('native-project').disabled=false;}
});
// Presets use actual traffic geometry and meaningful native P/N/D state.
$('aeb-demo').onclick=guarded(async()=>{await command('pause');await command('new_project',{scenario:'ev_interactive.json'});await command('controls',{values:{gear:2,throttle:0,assist_enabled:0,aeb_enabled:1,road_friction:.95}});await command('environment',{values:{speed:25}});await command('traffic_actor',{operation:'add',id:'stopped-lead',actor:{x:65,y:0,speed:0,target_speed:0}});await command('run');});
$('abs-demo').onclick=guarded(async()=>{await command('pause');await command('new_project',{scenario:'ev_interactive.json'});await command('controls',{values:{gear:2,throttle:0,brake:1,road_friction:.2,assist_enabled:0,aeb_enabled:0}});await command('environment',{values:{speed:25}});await command('run');});
const nativeCanvas=$('native-scene');let orbitDrag=null;
nativeCanvas.addEventListener('pointerdown',e=>{orbitDrag=e.clientX;nativeCanvas.setPointerCapture(e.pointerId);});nativeCanvas.addEventListener('pointermove',e=>{if(orbitDrag!==null){nativeOrbit+=(e.clientX-orbitDrag)*.008;orbitDrag=e.clientX;drawNativeScene();}});nativeCanvas.addEventListener('pointerup',()=>orbitDrag=null);nativeCanvas.addEventListener('pointercancel',()=>orbitDrag=null);nativeCanvas.addEventListener('wheel',e=>{e.preventDefault();nativeDistance=Math.max(5,Math.min(45,nativeDistance*(e.deltaY<0?.88:1/.88)));drawNativeScene();},{passive:false});
function drawNativeScene(){
  if(!state||!nativeMesh||!nativeCanvas.clientWidth)return;
  const scale=window.devicePixelRatio||1,width=nativeCanvas.clientWidth,height=nativeCanvas.clientHeight;nativeCanvas.width=width*scale;nativeCanvas.height=height*scale;const ctx=nativeCanvas.getContext('2d');ctx.scale(scale,scale);
  const w=state.world,x=w.x??w.position,y=w.y??0,heading=w.heading??0,yaw=heading+nativeOrbit,eye=[x-nativeDistance*Math.cos(yaw),y-nativeDistance*Math.sin(yaw),nativeDistance*.45+2],target=[x+3*Math.cos(heading),y+3*Math.sin(heading),1];
  const sub=(a,b)=>a.map((v,i)=>v-b[i]),dot=(a,b)=>a.reduce((s,v,i)=>s+v*b[i],0),cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]],unit=a=>{const l=Math.hypot(...a);return a.map(v=>v/(l||1));},forward=unit(sub(target,eye)),right=unit(cross(forward,[0,0,1])),up=cross(right,forward),focal=Math.min(width,height)*1.25;
  const project=v=>{const d=sub(v,eye),z=dot(d,forward);if(z<.2)return null;return [width*.5+focal*dot(d,right)/z,height*.52-focal*dot(d,up)/z,z];};
  const gradient=ctx.createLinearGradient(0,0,0,height);gradient.addColorStop(0,'#a1c0d1');gradient.addColorStop(1,'#8caa7b');ctx.fillStyle=gradient;ctx.fillRect(0,0,width,height);let faces=[];
  function polygon(vertices,color){const points=vertices.map(project);if(points.some(p=>!p))return;faces.push({points,color,z:points.reduce((s,p)=>s+p[2],0)/points.length});}
  function flush(){faces.sort((a,b)=>b.z-a.z);for(const f of faces){ctx.fillStyle=f.color;ctx.beginPath();f.points.forEach((p,i)=>i?ctx.lineTo(p[0],p[1]):ctx.moveTo(p[0],p[1]));ctx.closePath();ctx.fill();}faces=[];}
  for(let i=Math.floor(x/10)*10-80;i<x+150;i+=10)for(let j=Math.floor(y/10)*10-60;j<y+60;j+=10)polygon([[i,j,-.03],[i+10,j,-.03],[i+10,j+10,-.03],[i,j+10,-.03]],((i+j)/10)%2?'#7b946c':'#809b70');flush();
  for(let i=Math.floor(x/5)*5-80;i<x+160;i+=5){polygon([[i,-5.4,0],[i+5,-5.4,0],[i+5,5.4,0],[i,5.4,0]],'#52616b');if((i/5)%2===0)polygon([[i,-.04,.01],[i+3,-.04,.01],[i+3,.04,.01],[i,.04,.01]],'#e5e9df');}flush();
  function car(mesh,px,py,h,ego,travel){for(const t of mesh.triangles){const vertices=[];for(let k=0;k<9;k+=3){let [vx,vy,vz]=t.slice(k,k+3);if(t[15]){const spin=travel/(state.physics?.wheel_radius_m??.32),dx=vx-t[12],dz=vz-t[14];vx=t[12]+Math.cos(spin)*dx+Math.sin(spin)*dz;vz=t[14]-Math.sin(spin)*dx+Math.cos(spin)*dz;if(t[16]&&ego){const a=w.steering_angle??0,wx=vx-t[12],wy=vy-t[13];vx=t[12]+Math.cos(a)*wx-Math.sin(a)*wy;vy=t[13]+Math.sin(a)*wx+Math.cos(a)*wy;}}vertices.push([px+Math.cos(h)*vx-Math.sin(h)*vy,py+Math.sin(h)*vx+Math.cos(h)*vy,vz]);}const n=unit(cross(sub(vertices[1],vertices[0]),sub(vertices[2],vertices[0]))),light=.45+.55*Math.abs(dot(n,unit([-.4,-.3,1])));polygon(vertices,ego&&w.collision?'#de5567':`rgb(${t.slice(9,12).map(v=>Math.round(v*light)).join(',')})`);}}
  car(nativeMesh,x,y,heading,true,w.position);for(const a of state.traffic?.actors??[])car(nativeDefaultMesh??nativeMesh,a.x,a.y,a.yaw,false,a.x+a.y);flush();
  ctx.font='12px monospace';ctx.fillStyle='#f5f8ef';ctx.fillText(`${nativeMesh.name} · actual X ${number(x)} / Y ${number(y)} m`,12,height-14);for(const a of state.traffic?.actors??[]){const p=project([a.x,a.y,2.7]);if(p){ctx.fillStyle='#fff2c6';ctx.fillText(`${a.id} · ${number(a.speed*3.6)} km/h`,p[0],p[1]);}}
}

$('native-physics').onclick=guarded(async()=>{await command('pause');const project=await api('/api/project');Object.assign(project.scenario.plant,{model:'bicycle_grip_v2',mass_kg:nval('native-mass'),final_drive_ratio:nval('native-ratio'),brake_force_n:nval('native-brakes'),motor_torque_limit_nm:nval('native-torque')});await command('apply_project_draft',{project});physicsDraft=false;window.renderNative(state);toast('Physics deployed; experiment restarted.');});
$('native-edit-selected').onclick=()=>{const ecu=state.ecus.find(e=>e.name===selected);if(ecu){editDefinition(ecu.definition);tab('design');}};
$('native-deploy').onclick=guarded(async()=>{const definition=JSON.parse($('definition').value);await command('pause');await command('update_ecu',{name:definition.name,definition});$('validation-badge').textContent='DEPLOYED';toast('ECU program deployed to native workers; experiment restarted.');});
