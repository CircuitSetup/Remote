'use strict';
function choiceOptions(p){return String(p.options||'').split(';').map((label,value)=>({value,label,disabled:!label||value<p.minimum||value>p.maximum}));}
function editableField(p){return !!p.writable&&!p.hidden&&[0,1,2,3,9].includes(p.type);}
function uniqueChoice(p,label,unit){if(p.type!==9||String(p.unit||'')!==unit)throw Error('Choice units changed');const m=choiceOptions(p).filter(o=>!o.disabled&&o.label===label);if(m.length!==1)throw Error('Choice unavailable or ambiguous: '+label);return m[0].value;}
function bindingVerified(s){return !!(s.rfKnown&&s.connected&&s.modelMatched&&s.devices.some(d=>d.address===236));}
function descriptorMatches(p,q){return !!q&&['device','field','generation','type','value','hash'].every(key=>p[key]===q[key]);}
function channelOptions(p,prefix){const options=p.type===9?choiceOptions(p):[0,1,2,3].includes(p.type)&&Number.isInteger(p.minimum)&&Number.isInteger(p.maximum)&&p.maximum-p.minimum<=255?Array.from({length:Math.max(0,p.maximum-p.minimum+1)},(_,i)=>({value:p.minimum+i,label:String(p.minimum+i),disabled:false})):[];return options.map(o=>({...o,label:o.label.startsWith(prefix)?o.label:prefix+(prefix==='CH'?'':' ')+o.label}));}
if(typeof module!=='undefined')module.exports={choiceOptions,editableField,uniqueChoice,bindingVerified,descriptorMatches,channelOptions};
if(typeof document!=='undefined'){
 const $=id=>document.getElementById(id),sleep=ms=>new Promise(r=>setTimeout(r,ms)),number=input=>String(input.value).trim()?Number(input.value):NaN;
 const txControls=[['telemetry','Telemetry Ratio',''],['rfRate','Packet Rate','RF packet rate; separate from Remote CRSF channel cadence.'],['power','TX Power','Maximum configured power. Dynamic power may use less.'],['dynamic','Dynamic power','Automatic RF power.']];
 let nonce='',busy=false,radioActive=false,operation=null,roster=[],menus={},preview=null,modelId=null;
 const drafts=new Map(),owner=address=>roster.find(d=>d.address===Number(address)),fields=address=>menus[address]||[],deviceLabel=d=>d?d.name+' · '+d.address:'Not discovered';
 const pending=address=>[...drafts.values()].filter(d=>address===undefined||d.p.device===address),key=p=>p.device+':'+p.field;
 function message(t,error=false){$('message').textContent=t;if(error)$('message').focus();}
 function showStatus(s){$('safety').textContent=!s.crsfActive?'Radio controls are inactive.':'RF '+(s.rfKnown?(s.connected?'connected':'disconnected'):'unknown')+' · '+(s.armedKnown?(s.armed?'armed':'disarmed'):'arm state unknown')+' · Model Match '+(s.rfKnown&&s.connected?(s.modelMatched?'matched':'mismatch'):'unknown')+' · final CH5 '+s.ch5+' · status age '+s.statusAgeMs+' ms'+(s.throttleReady===false?' · Throttle output locked: move throttle to its calibrated center before throttle output.':'');}
 function unknownStatus(){$('safety').textContent='RF unknown · arm state unknown · Model Match unknown';}
 function enable(){
  $('remote-controls').dataset.busy=busy?'1':'0';
  for(const id of ['maintenance','backup','model','file','kind','preview','review'])$(id).disabled=!nonce||busy;
  for(const id of ['status','assign','txRefresh','rxRefresh'])$(id).disabled=!nonce||busy||!radioActive;
  $('apply').disabled=!nonce||busy||!preview;$('modelId').disabled=!nonce||busy||!radioActive||modelId===null;
  for(const[id,address]of[['txSave',238],['rxSave',236]])$(id).disabled=!nonce||busy||!radioActive||!pending(address).length;
  for(const id of ['txFields','rxFields'])for(const e of $(id).querySelectorAll('button,select,input'))e.disabled=!nonce||busy||!radioActive;
  if(typeof settingsSave!=='undefined'&&settingsSave)settingsSave.disabled=busy;
 }
 function one(list,key,value){const matches=list.filter(p=>p[key]===value);return matches.length===1?matches[0]:null;}
 function clearDrafts(address){for(const[k,d]of drafts)if(d.p.device===address)drafts.delete(k);}
 function stage(p,input,label){const value=number(input);if(value===p.value)drafts.delete(key(p));else drafts.set(key(p),{p:{...p},value,label});enable();}
 async function api(path,values=null){const headers=nonce?{'X-Remote-Nonce':nonce}:{},o={headers,credentials:'same-origin',cache:'no-store'};if(values){o.method='POST';o.body=values instanceof FormData?values:new URLSearchParams(values);}for(let attempt=0;;attempt++){let response,r;try{response=await fetch('/remote/'+path,o);r=await response.json();}catch(e){if(values){const error=new Error(e.message||'Request failed');error.transport=true;throw error;}if(!['operation','devices'].includes(path)||attempt)throw e;continue;}if(!response.ok)throw Error((r.code||'REQUEST_FAILED')+' '+(r.path||'')+': '+(r.message||'Rejected'));return r;}}
 async function renewSession(){const previous=nonce,s=await api('session',{});nonce=s.nonce;const replaced=!!previous&&previous!==nonce;if(replaced){preview=null;menus={};$('previewText').textContent='';renderViews();}return replaced;}
 async function guarded(fn){if(busy)return;busy=true;enable();try{if(await renewSession()){message('Session changed. Refresh before saving.',true);return;}await fn();}catch(e){preview=null;message(e.message,true);}finally{busy=false;enable();}}
 function clearOperation(){operation=null;$('confirm').hidden=true;$('cancel').hidden=true;}
 async function run(action,values={}){let accepted;try{accepted=await api('action',{action,...values,...(['read','status'].includes(action)?{wait:1}:{})});}catch(e){if(!e.transport||action!=='command'||!values.operationId)throw e;let s;try{s=await api('operation');}catch(readError){clearOperation();throw Error('Command response lost; status unreadable: '+readError.message);}if(s.id!==Number(values.operationId)){clearOperation();throw Error('Operation ownership changed; refresh/readback required');}operation=s;$('confirm').hidden=s.state!=='confirmation';$('cancel').hidden=!['confirmation','running'].includes(s.state);throw Error('Command response lost; state: '+s.state);}operation=accepted;let s=accepted;for(;;){if(!['complete','cancelled','failed','unknown','confirmation'].includes(s.state)){try{s=await api('operation');}catch(e){clearOperation();throw Error('Operation status could not be read: '+e.message);}}if(s.id!==operation.id){clearOperation();throw Error('Operation ownership changed; refresh/readback required');}operation=s;if(action==='status'&&s.state==='complete'&&s.radioStatus)showStatus(s.radioStatus);$('confirm').hidden=s.state!=='confirmation';$('cancel').hidden=!['confirmation','running'].includes(s.state);message(s.state+': '+(s.message||''));if(['complete','cancelled'].includes(s.state)){const p=s.parameter;if(s.state==='complete'&&action==='command'&&['Bind','Enter Bind Mode'].includes(p?.name)){menus={};await run('discover');await run('status');const link=await devices(false);if(!bindingVerified(link))throw Error('Bind acknowledged; verify receiver link and Model Match');message('Receiver connection and Model Match verified.');}return s.state==='complete'?p:null;}if(['failed','unknown'].includes(s.state))throw Error(s.message||s.error||s.state);if(s.state==='confirmation')return null;await sleep(180);s={id:s.id,state:'queued'};}}
 async function devices(render=true,owned=false){const s=await api('devices');if(owned)for(const d of drafts.values()){const previous=owner(d.p.device);if(previous&&(previous.generation===d.p.generation||previous.generation===d.ownedGeneration))d.ownedGeneration=s.devices.find(p=>p.address===d.p.device)?.generation;}radioActive=!!s.crsfActive;roster=s.devices;modelId=Number.isInteger(s.modelId)?s.modelId:null;for(const address of Object.keys(menus)){const d=owner(address);if(!d||menus[address].some(p=>p.generation!==d.generation))delete menus[address];}if(render)showStatus(s);renderViews();enable();return s;}
 async function read(p){const d=owner(p.device);if(!d||d.generation!==p.generation)throw Error('Device generation changed; refresh/readback required');const previous=operation;let actual;try{actual=await run('read',{device:p.device,field:p.field,generation:p.generation});}catch(e){if(operation===previous||operation?.state!=='failed'||operation.error!=='timeout'||operation.device!==p.device||operation.field!==p.field||operation.generation!==p.generation)throw e;const current=owner(p.device);if(!current||current.generation!==p.generation)throw Error('Device generation changed; refresh/readback required');actual=await run('read',{device:p.device,field:p.field,generation:p.generation});}if(!actual||actual.device!==p.device||actual.field!==p.field||actual.generation!==p.generation)throw Error('Readback unavailable; refresh required');return actual;}
 async function loadMenu(d){
  if(!d)throw Error('Device unavailable; refresh again');menus[d.address]=[];renderViews();const errors=[];
  for(let field=1;field<=d.fieldCount;field++){message('Reading '+d.name+' '+field+'/'+d.fieldCount);try{fields(d.address).push(await read({device:d.address,field,generation:d.generation}));renderViews();}catch(e){errors.push('Field '+field+': '+e.message);}}
  await devices(false);if(owner(d.address)?.generation!==d.generation)errors.push('Device generation changed; refresh required');
  if(errors.length)message('Readback incomplete; refresh. '+errors.join('; '),true);return !errors.length;
 }
 async function refresh(address){
  if(pending(address).length){if(!confirm('Discard unsaved '+(address===238?'transmitter':'receiver')+' changes and refresh?'))return false;clearDrafts(address);}
  await devices();if(!owner(address)){await run('discover');await devices();}
  const loaded=await loadMenu(owner(address)),readbackMessage=$('message').textContent;let statusError='';
  try{await run('status');}catch(e){statusError=e.message;unknownStatus();}
  if(!loaded)message(readbackMessage,true);else if(statusError)message('RF status unavailable: '+statusError,true);else message('Device menu readback verified.');return loaded;
 }
 async function change(p,value){
  let actual;try{actual=await run('write',{device:p.device,field:p.field,generation:p.generation,expected:p.value,value,type:p.type,hash:p.hash,confirmed:1});}catch(e){delete menus[p.device];throw e;}
  if(!actual||actual.device!==p.device||actual.field!==p.field||actual.type!==p.type||actual.value!==value){delete menus[p.device];throw Error('Write readback unverified; refresh required');}
  if(actual.generation!==p.generation)await devices(false,true);else menus[p.device]=fields(p.device).map(old=>old.field===actual.field?actual:old);return actual;
 }
 async function saveRadio(addresses,refreshAfter=true){
  const edits=pending().filter(d=>addresses.includes(d.p.device)).sort((a,b)=>!!a.model-!!b.model);if(!edits.length)return true;
  for(const d of edits){if(d.uncertain)throw Error('Save outcome uncertain; refresh first');if(!Number.isInteger(d.value)||d.value<(d.model?0:d.p.minimum)||d.value>(d.model?63:d.p.maximum)||(d.p.type===9&&!choiceOptions(d.p).some(o=>o.value===d.value&&!o.disabled)))throw Error('Invalid value for '+d.label);}
  await devices(false);for(const d of edits){const current=owner(d.p.device);if(!current||(current.generation!==d.p.generation&&current.generation!==d.ownedGeneration)||d.model&&modelId!==d.p.value)throw Error('Device changed; refresh before saving');}
  const caption=addresses.length>1?'Save All Radio Settings':addresses[0]===238?'Save Transmitter Settings':'Save Receiver Settings';
  if(!confirm(caption+'?\n'+edits.map(d=>deviceLabel(owner(d.p.device))+': '+d.label+' '+d.p.value+' → '+d.value).join('\n')))return false;
  await run('status');let saved=0;
  try{
   for(const d of edits){let p=d.p;
    if(d.model){await run('model',{generation:owner(238).generation,modelId:d.value});await devices(false,true);if(modelId!==d.value)throw Error('TX profile readback unverified');}
    else{if(p.generation!==owner(p.device)?.generation){const latest=await read({...p,generation:owner(p.device)?.generation});if(!descriptorMatches({...p,generation:latest.generation},latest)||!editableField(latest))throw Error(d.label+' changed; refresh before saving');p=latest;}await change(p,d.value);}
    drafts.delete(key(d.p));saved++;
   }
  }catch(e){if(edits[saved])edits[saved].uncertain=true;renderViews();throw Error('Saved '+saved+' of '+edits.length+' radio changes. '+e.message);}
  if(refreshAfter)for(const address of [...new Set(edits.map(d=>d.p.device))])if(!await loadMenu(owner(address)))throw Error('Changes saved; readback incomplete. Refresh.');
  renderViews();message('Radio settings saved and verified.');return true;
 }
 function renderField(target,p,label,help='',prefix=''){
  if(p?.hidden)return;const row=document.createElement('div'),title=document.createElement('strong');title.textContent=label;row.append(title);target.append(row);const info=document.createElement('p');info.textContent=help;row.append(info);
  if(!p){info.textContent+=(info.textContent?' ':'')+'Unavailable. Refresh if supported.';return;}
  if(p.type===11)return;
  const output=one(fields(236),'name','Output Ch'),inputContext=p.device!==236||p.name!=='Input Ch'||(output&&!output.hidden),canEdit=editableField(p)&&inputContext;
  const note=document.createElement('small');note.textContent=p.type===13?p.info||'Device command.':'Verified value: '+([10,12].includes(p.type)?p.info??'':p.type===9?choiceOptions(p)[p.value]?.label??'(unavailable)':p.value)+' '+(p.unit||'');row.append(note);
  if(canEdit){const choices=prefix?channelOptions(p,prefix):p.type===9?choiceOptions(p):null;if(choices&&!choices.length){note.textContent='Unsupported range.';return;}const input=document.createElement(choices?'select':'input');input.setAttribute('aria-label',label);if(choices)for(const o of choices){const opt=new Option(o.label||'(unavailable)',o.value);opt.disabled=o.disabled;input.add(opt);}else{input.type='number';input.min=p.minimum;input.max=p.maximum;input.step=1;}input.value=drafts.get(key(p))?.value??p.value;row.append(input);
   if(p.device===236&&p.name==='Output Ch')input.onchange=()=>guarded(async()=>{const value=number(input);if(pending(236).length){if(!confirm('Discard unsaved receiver changes before loading a different output?')){input.value=p.value;return;}clearDrafts(236);}const latest=await read(p);if(!descriptorMatches(p,latest))throw Error('Output selector changed; refresh required');await run('status');await change(p,value);if(!await loadMenu(owner(236)))throw Error('Output readback incomplete; refresh');message('Output loaded. Edit and save Receiver Settings.');});
   else input.oninput=()=>stage(p,input,label);
  }else if(p.type===13&&p.commandAllowed){const start=document.createElement('button');start.type='button';start.textContent=p.name==='Set Failsafe Pos'?'Capture failsafe positions':'Start '+label;start.onclick=()=>guarded(async()=>{if(pending().length)throw Error('Save pending radio settings before starting a command');if(!confirm(p.name==='Set Failsafe Pos'?'Store current positions for all configured PWM outputs as failsafe positions?':'Start '+label+'?'))return;const latest=await read(p);if(!descriptorMatches(p,latest)||!latest.commandAllowed)throw Error('Command changed; refresh required');await run('status');await run('command',{device:p.device,field:p.field,generation:p.generation,status:1,confirmed:1});});row.append(start);}
  else{const unavailable=document.createElement('small');unavailable.textContent='Read only or unsupported.';row.append(unavailable);}
 }
 function renderViews(){
  for(const[id,address]of[['txDevice',238],['rxDevice',236]])$(id).textContent='Device: '+deviceLabel(owner(address));
  $('modelId').value=drafts.get('238:0')?.value??modelId??'';
  $('txFields').replaceChildren();const shown=new Set();for(const[role,label,help]of txControls){const p=one(fields(238),'role',role);if(p)shown.add(p.field);renderField($('txFields'),p,label,help);}
  for(const p of fields(238))if(!shown.has(p.field))renderField($('txFields'),p,p.name);
  $('rxFields').replaceChildren();for(const p of fields(236)){const output=p.name==='Output Ch',input=p.name==='Input Ch',failsafe=p.name==='Set Failsafe Pos';renderField($('rxFields'),p,output?'Physical PWM output':input?'Incoming RC channel':failsafe?'Failsafe positions':p.name,output?'Save pending changes before switching outputs.':input?'RC channel for this output.':failsafe?'Stores positions for all configured PWM outputs.':'',output?'Output':input?'CH':'');}enable();
 }
 $('txRefresh').onclick=()=>guarded(()=>refresh(238));$('rxRefresh').onclick=()=>guarded(()=>refresh(236));
 $('txSave').onclick=()=>guarded(()=>saveRadio([238]));$('rxSave').onclick=()=>guarded(()=>saveRadio([236]));
 $('modelId').oninput=()=>{const p={device:238,field:0,generation:owner(238)?.generation,value:modelId},value=number($('modelId'));if(value===modelId)drafts.delete(key(p));else drafts.set(key(p),{p,value,label:'TX profile ID',model:true});enable();};
 $('maintenance').onclick=()=>guarded(async()=>{await api('action',{action:'maintenance',enabled:1});message('Maintenance enabled for five minutes.');});
 $('status').onclick=()=>guarded(async()=>{await run('status');await devices(false);});
 $('assign').onclick=()=>guarded(async()=>{if(pending().length)throw Error('Save pending radio settings before assigning a receiver');const d=owner(236);if(!d||modelId===null)throw Error('Refresh transmitter and receiver readback first');if(!confirm('Assign this receiver to saved TX profile '+modelId+'?'))return;await run('status');await run('assign',{device:d.address,generation:d.generation,modelId});await devices();await loadMenu(owner(236));});
 for(const[id,status]of[['confirm',4],['cancel',5]])$(id).onclick=async()=>{const old=operation;if(!old){message('Operation ownership changed; refresh/readback required',true);return;}const values={device:old.device,field:old.field,generation:old.generation,operationId:old.id,status};if(busy){try{await api('action',{action:'command',...values});}catch(e){message(e.message,true);}return;}return guarded(()=>run('command',values));};
 if(typeof settingsSave!=='undefined'&&settingsSave){
  settingsSave.textContent='Save All Settings';const formState=()=>new URLSearchParams(new FormData(settingsForm)).toString();let initialForm=formState(),resuming=false;document.addEventListener('DOMContentLoaded',()=>initialForm=formState(),{once:true});
  settingsForm.addEventListener('submit',async event=>{if(resuming)return;const localChanged=formState()!==initialForm;if(!pending().length){if(!localChanged){event.preventDefault();message('No unsaved changes.');}return;}event.preventDefault();event.stopImmediatePropagation();await guarded(async()=>{if(!await saveRadio([238,236],!localChanged))return;if(localChanged){resuming=true;settingsSave.disabled=false;settingsForm.requestSubmit(settingsSave);resuming=false;}});},true);
 }
 function saveFile(blob,name){const a=document.createElement('a'),url=URL.createObjectURL(blob);a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}
 async function download(kind,name){const r=await fetch('/remote/export?kind='+kind,{headers:{'X-Remote-Nonce':nonce},cache:'no-store'});if(!r.ok){const e=await r.json();throw Error(e.message||'Export failed');}saveFile(await r.blob(),name);}
 $('backup').onclick=()=>guarded(()=>download('backup','remote.crsf-backup.json'));$('model').onclick=()=>guarded(()=>download('model','remote.remote-model.json'));
 $('preview').onclick=()=>guarded(async()=>{const file=$('file').files[0];if(!file)throw Error('Choose a native CRSF file');const kind=$('kind').value;if(file.size>16384)throw Error('File exceeds its size limit');const form=new FormData();form.append('file',file);preview=await api('upload?kind='+kind,form);$('previewText').textContent=JSON.stringify(preview,null,2);message('Review CRSF settings and calibration before activation.');});
 $('apply').onclick=()=>guarded(async()=>{if(pending().length)throw Error('Save pending radio settings before activating a file');if(!preview||!confirm('Activate this exact preview and restart? '+preview.calibration))return;await devices();if(radioActive)await run('status');await api('apply',{fileHash:preview.fileHash,confirmed:1});preview=null;message('Settings saved. Reconnect after restart.');});
 $('review').onclick=()=>guarded(async()=>{await devices();if(radioActive){await refresh(238);for(const role of ['power','telemetry','dynamic']){const p=one(fields(238),'role',role);if(!p||p.hidden||p.type!==9)throw Error('TX radio review unavailable; refresh/readback required');await read(p);}await run('status');}await api('review',{confirmed:1});message('Radio review verified.');});
 (async()=>{enable();try{await renewSession();await devices();$('access').textContent='Connected.';message('Ready. Refresh to load settings.');}catch(e){$('access').textContent='Read only. Session unavailable.';message(e.message);}enable();})();
}
