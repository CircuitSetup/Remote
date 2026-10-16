'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),zlib=require('node:zlib');
const root=path.join(__dirname,'../src/src/CRSF');
const script=fs.readFileSync(path.join(root,'crsf_config.js'),'utf8').replace(/\r\n/g,'\n');
const html=fs.readFileSync(path.join(root,'crsf_config.html'),'utf8');
const wifi=fs.readFileSync(path.join(root,'crsf_wifi.h'),'utf8');
const uiHeader=fs.readFileSync(path.join(root,'crsf_config_ui.h'),'utf8');
const embedded=zlib.gunzipSync(Buffer.from([...uiHeader.matchAll(/0x([0-9a-f]{2})/gi)].map(m=>parseInt(m[1],16)))).toString('utf8');
function portalLayout(source){
 let last=-1;
 for(const section of ['Live ELRS controls','Transmitter','Receiver','CRSF settings files','External radio settings']){
  const at=source.indexOf('<h3>'+section+'</h3>');assert(at>last,section+' section is missing or out of order');last=at;
 }
 assert.doesNotMatch(source,/<h3>Radio devices<\/h3>/);
 assert(source.indexOf('id="review"')>source.indexOf('<h3>External radio settings</h3>'));
 assert.match(wifi,/custom_remote_controls\("<div id='remote-controls'><\/div><script defer src='\/remote\/ui\.js'><\/script>"\)/);
 const start=wifi.indexOf('WiFiManagerParameter * const crsfParmArray[]'),parameters=wifi.slice(start,wifi.indexOf('NULL',start));
 assert(parameters.trimEnd().endsWith('&custom_remote_controls,'),'controls precede native footer Save');
 assert.doesNotMatch(wifi,/WiFiManagerParameter custom_\w*(?:tlm|power|dynp)/i);
 assert.doesNotMatch(html,/(?:ctlmr|cmpwr|cdynp|elrsTlmRatio|elrsMaxPower)/);
 assert.doesNotMatch(source,/<button(?![^>]*\btype=['"]button['"])/i);
}
class Element{
 constructor(tag='div'){this.tag=tag;this.children=[];this.options=[];this.value='';this.dataset={};this.textContent='';this.files=[];this.hidden=false;this.disabled=false;this.parentNode=null;this.attributes={};}
 add(node){this.options.push(node);if(this.options.length===1)this.value=node.value;}
 get selectedOptions(){return this.options.filter(o=>o.value===String(this.value));}
 replaceChildren(...nodes){this.children=[];this.options=[];this.value='';this.append(...nodes);}
 append(...nodes){for(const node of nodes){if(node.parentNode){const list=node.parentNode.children,i=list.indexOf(node);if(i>=0)list.splice(i,1);}node.parentNode=this;this.children.push(node);}}
 after(...nodes){if(!this.parentNode)return;const parent=this.parentNode;let at=parent.children.indexOf(this)+1;for(const node of nodes){if(node.parentNode){const old=node.parentNode.children.indexOf(node);node.parentNode.children.splice(old,1);if(node.parentNode===parent&&old<at)at--;}node.parentNode=parent;parent.children.splice(at++,0,node);}}
 closest(selector){for(let n=this;n;n=n.parentNode)if(selector==='form'&&n.tag==='form')return n;return null;}
 querySelector(selector){return selector==='button[type=submit]'?this.children.find(n=>n.tag==='button'&&n.type==='submit')||null:null;}
 querySelectorAll(selector){return descendants(this).filter(n=>selector==='button,select,input'&&['button','select','input'].includes(n.tag));}
 getAttribute(name){return this.attributes[name]??(name==='action'?this.action:null);}
 setAttribute(name,value){this.attributes[name]=String(value);}
 focus(){}
 addEventListener(name,handler){this.handlers??={};this.handlers[name]=handler;}
 async submit(){const event={preventDefault(){this.defaultPrevented=true;},stopImmediatePropagation(){}};await this.handlers?.submit?.(event);if(!event.defaultPrevented)this.submits=(this.submits||0)+1;}
 requestSubmit(){this.lastSubmit=this.submit();}
 click(){}
}
class Option{constructor(label,value){this.textContent=label;this.value=String(value);this.dataset={};}}
const descendants=n=>n.children.flatMap(c=>[c,...descendants(c)]);
const textOf=n=>[n.textContent,...n.children.map(textOf)].join(' ');
const inputFor=(f,id,label)=>descendants(f.elements[id]).find(n=>n.attributes['aria-label']===label);
const buttonFor=(f,id,label)=>descendants(f.elements[id]).find(n=>n.tag==='button'&&n.textContent===label);
function parameter(device,field,name,extra={}){
 return {device,field,name,generation:device===238?3:7,type:9,value:0,minimum:0,maximum:2,options:'First;Second;Third',unit:'',hash:field*17,hidden:false,writable:true,role:'',...extra};
}
function fixture({crsfActive=true,localAP=true,formAction='/param3save',absentTX=false,transform=()=>{},fieldCount=4,operationAdvanceMs=0,failGetOnce='',staleDeviceStatus=false,rejectStatus=false,inlineReads=true}={}){
 const elements={},calls=[],timers=[],confirmations=[];
 let clock=0,sessionNonce='',sessionExpires=0;
 const body=new Element('body'),form=new Element('form'),rootNode=new Element('div'),save=new Element('button');
 form.action=formAction;form.values={local:'0'};form.submits=0;rootNode.id='remote-controls';save.type='submit';body.append(form);form.append(rootNode,save);
 const document={readyState:'complete',handlers:{},addEventListener(name,handler){this.handlers[name]=handler;},getElementById:id=>elements[id]??=(new Element()),createElement:tag=>new Element(tag)};
 elements['remote-controls']=rootNode;
 for(const id of ['access','message','maintenance','status','discover','selectModel','assign','backup','model','preview','review','apply','kind','file','previewText','safety','devices','fields','field','modelId','confirm','cancel','txSave','rxSave','txRefresh','txFields','rxRefresh','rxFields'])elements[id]=new Element(id==='file'?'input':id.endsWith('Fields')?'div':id==='devices'||id==='fields'?'select':'button');
 elements.modelId.value='0';
  const radio={txGeneration:3,rxGeneration:7,txPresent:!absentTX,failRead:null,failReadOnce:null,failWrite:false,failPostAction:null,loseCommandReplyStatus:null,failGetPath:null,mismatchOperationId:null,changedPreview:null,throttleReady:false,modelId:0,appliedWrites:[],confirm:true};
 const params=[
  parameter(238,1,'Telem Ratio',{role:'telemetry',value:2,options:'Off;;1:64;1:128',maximum:3}),
  parameter(238,2,'Max Power',{role:'power',value:1,options:'25;100;250',unit:'mW'}),
  parameter(238,3,'Dynamic',{role:'dynamic',value:3,options:'Off;On;;AUX9',maximum:3}),
  parameter(238,4,'Packet Rate',{role:'rfRate',value:1,options:'50Hz;333Hz Full;1000Hz'}),
  parameter(236,1,'Output Ch',{type:0,value:1,minimum:1,maximum:4,options:''}),
  parameter(236,2,'Input Ch',{type:0,value:3,minimum:1,maximum:16,options:''}),
  parameter(236,3,'Set Failsafe Pos',{type:13,writable:false,commandAllowed:true,info:'Capture PWM positions'}),
  parameter(236,4,'Read-only value',{writable:false}),parameter(236,5,'Hidden value',{hidden:true})
 ];
 for(let field=5;field<=fieldCount;field++)params.push(parameter(238,field,'TX field '+field,{writable:false,role:''}));
 transform(params,radio);
 let current={state:'idle'},sequence=0;
 const generation=d=>d===238?radio.txGeneration:radio.rxGeneration;
 async function fetch(url,options={}){
  const pathname=url.split('?')[0],values=options.body instanceof URLSearchParams?Object.fromEntries(options.body):{};
  calls.push({path:pathname,url,method:options.method||'GET',headers:options.headers,...values});let data={},ok=true;
   if((options.method||'GET')==='GET'&&failGetOnce===pathname){failGetOnce='';throw Error('transient GET failure');}
   if((options.method||'GET')==='GET'&&radio.failGetPath===pathname)throw Error('operation status transport failure');
   if(pathname==='/remote/action'&&values.action===radio.failPostAction){radio.failPostAction=null;throw Error('ambiguous POST failure');}
  const gated=['/remote/action','/remote/operation','/remote/export','/remote/upload','/remote/apply','/remote/review'].includes(pathname);
  if(pathname==='/remote/session'){
   const supplied=options.headers?.['X-Remote-Nonce'];
   if(!sessionNonce)sessionNonce='a'.repeat(32);else if(supplied!==sessionNonce)sessionNonce='b'.repeat(32);
   sessionExpires=clock+300000;data={nonce:sessionNonce};
  }else if(gated&&(options.headers?.['X-Remote-Nonce']!==sessionNonce||clock>=sessionExpires)){
   ok=false;data={code:'ACCESS_DENIED',path:'session',message:'Connect through Remote and use a current session'};
  }else if(gated)sessionExpires=clock+300000;
   if(pathname==='/remote/devices')data={crsfActive,localAP,throttleReady:radio.throttleReady,rfKnown:true,connected:true,modelMatched:true,modelId:radio.modelId,armedKnown:true,armed:false,ch5:172,statusAgeMs:staleDeviceStatus?5147:10,devices:!crsfActive?[]:[...(radio.txPresent?[{address:238,name:'TX',fieldCount,generation:radio.txGeneration}]:[]),{address:236,name:'RX',fieldCount:5,generation:radio.rxGeneration}]};
  else if(pathname==='/remote/upload')data={fileHash:'b'.repeat(64),calibration:"Keep this Remote's calibration"};
  else if(pathname==='/remote/apply'||pathname==='/remote/review')data={ok:true};
  else if(pathname==='/remote/action'&&values.action==='status'&&(rejectStatus||current.state==='confirmation')){ok=false;data={code:rejectStatus?'UNSUPPORTED_BEHAVIOR':'BUSY',path:'radio',message:rejectStatus?'RF status is unavailable for this transmitter':'Command confirmation is pending'};}
  else if(pathname==='/remote/action'){
   const device=Number(values.device),field=Number(values.field),p=params.find(p=>p.device===device&&p.field===field);
    let loseCommandReply=false;
    current={id:values.operationId?Number(values.operationId):++sequence,state:'complete',action:values.action,device,field,generation:generation(device)};
   if(values.action==='discover')radio.txPresent=true;
   if(values.action==='model'){radio.modelId=Number(values.modelId);radio.txGeneration=radio.rxGeneration=9;}
   if(['read','write','command'].includes(values.action)){
    if(Number(values.generation)!==generation(device)){current.state='failed';current.message='Stale generation';}
    else if(!p){current.state='failed';current.message='Missing field';}
     else if((radio.failRead===device+':'+field||radio.failReadOnce===device+':'+field)&&values.action==='read'){if(radio.failReadOnce===device+':'+field)radio.failReadOnce=null;current.state='failed';current.error='timeout';current.message='Read timeout';}
    else{
     if(['read','write'].includes(values.action)&&radio.changedPreview)Object.assign(p,radio.changedPreview);
     if(values.action==='write'){
      if(radio.failWrite){current.state='unknown';current.message='Write outcome unknown';}
      else if(!p.writable||p.type!==Number(values.type)||p.value!==Number(values.expected)||p.hash!==Number(values.hash)){current.state='failed';current.message='Field changed; reread required';}
      else{radio.appliedWrites.push({...values});p.value=Number(values.value);if(p.name==='Output Ch'||p.role==='rfRate'){radio.txGeneration=radio.rxGeneration=9;if(p.name==='Output Ch')params.find(p=>p.device===236&&p.name==='Input Ch').value=8;}if(p.role==='power')params.find(p=>p.role==='dynamic').options='Off;On;AUX5;AUX9';}
     }
      if(values.action==='command'){current.state=Number(values.status)===1?'confirmation':Number(values.status)===5?'cancelled':'complete';if(radio.loseCommandReplyStatus===Number(values.status)){radio.loseCommandReplyStatus=null;loseCommandReply=true;}}
     current.generation=generation(device);current.parameter={...p,generation:generation(device)};if(values.action==='read'&&radio.advanceOnRead===device+':'+field)radio.txGeneration=4;
    }
   }
   data=inlineReads&&values.wait==='1'&&['read','status'].includes(values.action)?{...current,...(current.action==='status'?{radioStatus:{crsfActive,rfKnown:true,connected:true,modelMatched:true,armedKnown:true,armed:false,statusAgeMs:12,ch5:172,throttleReady:radio.throttleReady}}:{})}:{id:current.id,state:'queued'};if(data.state==='complete')clock+=operationAdvanceMs;
    if(loseCommandReply)throw Error('ambiguous command POST');
   }else if(pathname==='/remote/operation'){data={...current,...(current.action==='status'?{radioStatus:{crsfActive,rfKnown:true,connected:true,modelMatched:true,armedKnown:true,armed:false,statusAgeMs:12,ch5:172,throttleReady:radio.throttleReady}}:{})};if(radio.mismatchOperationId!==null){data.id=radio.mismatchOperationId;radio.mismatchOperationId=null;}clock+=operationAdvanceMs;}
  return {ok,json:async()=>structuredClone(data),blob:async()=>new Blob(['{}'])};
 }
 class PortalFormData extends FormData{constructor(form){super();if(form)for(const [key,value]of Object.entries(form.values))this.append(key,value);}}
 vm.runInNewContext(embedded,{document,module:{exports:{}},Option,fetch,URLSearchParams,FormData:PortalFormData,confirm:message=>{confirmations.push(message);return radio.confirm;},URL:{createObjectURL:()=>'',revokeObjectURL:()=>{}},setTimeout:fn=>{timers.push(fn);return timers.length;}});
 return {elements,calls,timers,document,rootNode,body,form,save,radio,params,confirmations,advanceTime:ms=>{clock+=ms;},elapsed:()=>clock,replaceRemoteSession:()=>{sessionNonce='b'.repeat(32);sessionExpires=clock+300000;}};
}
const settle=()=>new Promise(setImmediate);

function stage(f,section,label,value){const input=inputFor(f,section,label);assert(input,label+' must be shown');input.value=String(value);assert.equal(typeof input.oninput,'function');input.oninput();return input;}
async function sectionSavesAndWholePage(){
 const f=fixture({fieldCount:5,transform:params=>Object.assign(params.find(p=>p.device===238&&p.field===5),{name:'Fan Thresh',writable:true})});await settle();
 assert.equal(typeof f.elements.txSave.onclick,'function','Transmitter has one section Save');
 await f.elements.txRefresh.onclick();await f.elements.rxRefresh.onclick();
 assert(inputFor(f,'txFields','Fan Thresh'),'other editable advertised fields appear under Transmitter');
 assert(!descendants(f.elements.txFields).some(e=>e.tag==='button'&&e.textContent.startsWith('Apply ')),'fields do not have individual Apply buttons');
 const before=f.calls.length;stage(f,'txFields','TX Power',2);stage(f,'txFields','Telemetry Ratio',3);
 assert(!f.calls.slice(before).some(c=>c.action==='write'),'edits remain staged');
 f.radio.confirm=false;await f.elements.txSave.onclick();assert(!f.calls.slice(before).some(c=>c.action==='write'),'cancel preserves drafts without writes');
 assert.equal(Number(inputFor(f,'txFields','TX Power').value),2);
 f.radio.confirm=true;await f.elements.txSave.onclick();
 const writes=f.calls.slice(before).filter(c=>c.action==='write');assert.equal(writes.length,2);assert(writes.every(c=>c.device==='238'));
 assert.equal(f.confirmations.filter(message=>/Save Transmitter Settings/.test(message)).length,2,'one confirmation per section save, including cancellation');
 assert(f.elements.txSave.disabled,'clean transmitter section needs no save');
 const unchanged=f.calls.length;await f.form.submit();assert.equal(f.form.submits,0,'clean page does not restart');assert(!f.calls.slice(unchanged).some(c=>c.action==='write'));
 stage(f,'rxFields','Incoming RC channel',4);stage(f,'txFields','Fan Thresh',2);f.form.values.local='1';
 await f.form.submit();await settle();assert.equal(f.form.submits,1,'footer saves dirty local settings after both radio sections');
 assert(f.calls.some(c=>c.action==='write'&&c.device==='236'&&c.field==='2'&&c.value==='4'));
 assert(f.calls.some(c=>c.action==='write'&&c.device==='238'&&c.field==='5'&&c.value==='2'));
 const failed=fixture();await settle();await failed.elements.txRefresh.onclick();stage(failed,'txFields','TX Power',2);failed.form.values.local='1';failed.radio.failWrite=true;
 await failed.form.submit();await settle();assert.equal(failed.form.submits,0,'radio failure prevents local save/restart');assert.match(failed.elements.message.textContent,/unknown|failed/i);
 const inline=fixture();await settle();await inline.elements.txRefresh.onclick();assert(!inline.calls.some(c=>c.path==='/remote/operation'),'completed inline reads avoid a second HTTP request');
 const initialized=fixture();await settle();initialized.form.values.logic='built after deferred scripts';initialized.document.handlers.DOMContentLoaded?.();await initialized.form.submit();assert.equal(initialized.form.submits,0,'native controls initialized on DOMContentLoaded are part of the saved baseline');
}

async function blankNumbersAreInvalid(){
 const field=fixture({fieldCount:5,transform:params=>Object.assign(params.find(p=>p.device===238&&p.field===5),{name:'Fan Thresh',writable:true,type:0,value:1,minimum:0,maximum:255})});await settle();await field.elements.txRefresh.onclick();
 stage(field,'txFields','Fan Thresh','');await field.elements.txSave.onclick();assert.equal(field.radio.appliedWrites.length,0,'blank numeric field must not save zero');assert.match(field.elements.message.textContent,/Invalid value/);
 const profile=fixture({transform:(_params,radio)=>radio.modelId=12});await settle();profile.elements.modelId.value='';profile.elements.modelId.oninput();await profile.elements.txSave.onclick();
 assert(!profile.calls.some(c=>c.action==='model'),'blank profile must not select profile zero');assert.match(profile.elements.message.textContent,/Invalid value/);
}

async function generationAndProfileSaves(){
 const f=fixture();await settle();await f.elements.txRefresh.onclick();await f.elements.rxRefresh.onclick();
 stage(f,'rxFields','Incoming RC channel',4);stage(f,'txFields','Packet Rate',2);stage(f,'txFields','TX Power',2);const before=f.calls.length;await f.elements.txSave.onclick();
 assert.equal(f.radio.appliedWrites.length,2,'section saves can continue across owned descriptor invalidation');
 assert(f.calls.slice(before).some(c=>c.action==='read'&&c.field==='2'&&c.generation==='9'),'remaining staged field is revalidated at the new generation');
 assert.equal(inputFor(f,'rxFields','Incoming RC channel'),undefined,'TX critical changes invalidate stale receiver editors');
 await f.elements.rxSave.onclick();assert(f.radio.appliedWrites.some(w=>w.device==='236'&&w.field==='2'&&w.value==='4'&&w.generation==='9'),'pending receiver draft survives a separately saved TX generation change');
 const dependent=fixture();await settle();await dependent.elements.txRefresh.onclick();await dependent.elements.rxRefresh.onclick();stage(dependent,'rxFields','Incoming RC channel',4);stage(dependent,'txFields','Packet Rate',2);await dependent.elements.txSave.onclick();
 dependent.params.find(p=>p.device===236&&p.field===2).value=8;await dependent.elements.rxSave.onclick();assert(!dependent.radio.appliedWrites.some(w=>w.device==='236'),'changed receiver value is not silently adopted after TX save');assert.match(dependent.elements.message.textContent,/changed.*refresh/i);
 const external=fixture();await settle();await external.elements.txRefresh.onclick();await external.elements.rxRefresh.onclick();stage(external,'rxFields','Incoming RC channel',4);external.radio.rxGeneration=8;stage(external,'txFields','Packet Rate',2);await external.elements.txSave.onclick();await external.elements.rxSave.onclick();
 assert(!external.radio.appliedWrites.some(w=>w.device==='236'),'an intervening external generation cannot be adopted by an owned TX save');
 const profile=fixture();await settle();await profile.elements.txRefresh.onclick();profile.elements.modelId.value='12';profile.elements.modelId.oninput();
 assert(!profile.calls.some(c=>c.action==='model'),'profile selection is staged');await profile.elements.txSave.onclick();
 assert(profile.calls.some(c=>c.action==='model'&&c.modelId==='12'&&c.generation==='3'));
 const changed=fixture();await settle();await changed.elements.txRefresh.onclick();stage(changed,'txFields','TX Power',2);changed.radio.txGeneration=4;
 const start=changed.calls.length;await changed.elements.txSave.onclick();assert(!changed.calls.slice(start).some(c=>c.action==='write'),'external generation change prevents writes');
 assert.match(changed.elements.message.textContent,/changed.*refresh/i);
}

async function transientReadsAndFreshStatus(){
 const retry=fixture({inlineReads:false,failGetOnce:'/remote/operation'});await settle();await retry.elements.txRefresh.onclick();
 assert(inputFor(retry,'txFields','TX Power'));assert.equal(retry.calls.filter(c=>c.action==='read'&&c.field==='2').length,1,'GET retry never reposts a field read');
 const readRetry=fixture();await settle();readRetry.radio.failReadOnce='238:2';await readRetry.elements.txRefresh.onclick();
 assert(inputFor(readRetry,'txFields','TX Power'));assert.equal(readRetry.calls.filter(c=>c.action==='read'&&c.field==='2').length,2,'matching terminal read timeout retries once');
 const devicesRetry=fixture({failGetOnce:'/remote/devices'});await settle();await devicesRetry.elements.txRefresh.onclick();assert(inputFor(devicesRetry,'txFields','TX Power'));
 const ambiguousRead=fixture();await settle();ambiguousRead.radio.failPostAction='read';await ambiguousRead.elements.txRefresh.onclick();
 assert.equal(ambiguousRead.calls.filter(c=>c.action==='read'&&c.field==='1').length,1,'ambiguous read POST is not replayed');assert.equal(inputFor(ambiguousRead,'txFields','Telemetry Ratio'),undefined);
 const ambiguous=fixture();await settle();await ambiguous.elements.txRefresh.onclick();ambiguous.radio.failPostAction='write';stage(ambiguous,'txFields','TX Power',2);await ambiguous.elements.txSave.onclick();
 assert.equal(ambiguous.calls.filter(c=>c.action==='write').length,1,'ambiguous write POST is never replayed');assert.doesNotMatch(ambiguous.elements.message.textContent,/verified/i);
 await ambiguous.elements.txSave.onclick();assert.equal(ambiguous.calls.filter(c=>c.action==='write').length,1,'uncertain draft requires refresh before retry');
 const fresh=fixture({staleDeviceStatus:true});await settle();await fresh.elements.txRefresh.onclick();assert.match(fresh.elements.safety.textContent,/status age 12 ms/);assert.doesNotMatch(fresh.elements.safety.textContent,/5147 ms/);
 const unsupported=fixture({rejectStatus:true});await settle();await unsupported.elements.txRefresh.onclick();assert(inputFor(unsupported,'txFields','TX Power'));assert.match(unsupported.elements.safety.textContent,/RF unknown.*arm state unknown.*Model Match unknown/i);assert.match(unsupported.elements.message.textContent,/RF status is unavailable/i);
 const partial=fixture({rejectStatus:true});await settle();partial.radio.failRead='238:4';await partial.elements.txRefresh.onclick();assert.match(partial.elements.message.textContent,/Readback incomplete.*Field 4/i);
}

async function sessionRenewal(){
 const idle=fixture();await settle();assert.equal(idle.calls.filter(c=>c.path==='/remote/session').length,1);
 idle.advanceTime(300001);const before=idle.calls.length;await idle.elements.txRefresh.onclick();
 assert.equal(idle.calls.slice(before).filter(c=>c.path==='/remote/session').length,1,'user operation renews an expired idle session first');
 assert.equal(idle.calls.slice(before).filter(c=>c.path==='/remote/action'&&c.action==='read').length,4,'expired session renewal preserves the intended readback operation');
 assert.equal(inputFor(idle,'txFields','TX Power').value,1);
 const long=fixture({fieldCount:20,operationAdvanceMs:30000});await settle();await long.elements.txRefresh.onclick();
 assert(long.elapsed()>300000,'sequential reads exceed five minutes');
 assert.equal(long.calls.filter(c=>c.path==='/remote/action'&&c.action==='read').length,20,'sliding operation polls keep a long menu readback authenticated');
 assert(!long.calls.some(c=>c.path==='/remote/action'&&c.action==='read'&&!c.headers['X-Remote-Nonce']),'long reads retain the session nonce');
 const same=fixture();await settle();same.elements.file.files=[new Blob(['{}'])];same.elements.kind.value='backup';await same.elements.preview.onclick();assert.equal(same.elements.apply.disabled,false);
 const sessionCount=same.calls.filter(c=>c.path==='/remote/session').length;await same.elements.status.onclick();
 assert.equal(same.calls.filter(c=>c.path==='/remote/session').length,sessionCount+1,'each guarded action renews the session');
 assert.equal(same.elements.apply.disabled,false,'same-token renewal keeps the exact validated preview available');
 const replaced=fixture();await settle();replaced.elements.file.files=[new Blob(['{}'])];replaced.elements.kind.value='backup';await replaced.elements.preview.onclick();assert.equal(replaced.elements.apply.disabled,false);
 replaced.replaceRemoteSession();const start=replaced.calls.length;await replaced.elements.apply.onclick();
 assert.equal(replaced.calls.slice(start).filter(c=>c.path==='/remote/session').length,1,'operation detects replaced token before running');
 assert(!replaced.calls.slice(start).some(c=>c.path==='/remote/devices'||c.path==='/remote/apply'),'replacement aborts stale apply instead of replaying it');
 assert.equal(replaced.elements.apply.disabled,true,'token replacement invalidates the local preview');
 const staleMutation=fixture();await settle();await staleMutation.elements.txRefresh.onclick();stage(staleMutation,'txFields','TX Power',2);
 staleMutation.replaceRemoteSession();const mutationStart=staleMutation.calls.length;await staleMutation.elements.txSave.onclick();
 assert(!staleMutation.calls.slice(mutationStart).some(c=>c.path==='/remote/action'),'replaced session aborts stale radio mutation without replay');
 assert.match(staleMutation.elements.message.textContent,/session changed/i);
}
async function liveControls(){
 const f=fixture({absentTX:true});await settle();assert.equal(f.elements.txDevice.textContent,'Device: Not discovered');await f.elements.txRefresh.onclick();
 assert(f.calls.some(c=>c.action==='discover'));assert.match(f.elements.safety.textContent,/calibrated center/i);
 assert.equal(inputFor(f,'txFields','Telemetry Ratio').value,2);assert.equal(inputFor(f,'txFields','Telemetry Ratio').options[1].disabled,true);
 assert.equal(inputFor(f,'txFields','Packet Rate').options[1].textContent,'333Hz Full');assert.equal(inputFor(f,'txFields','Dynamic power').options[3].textContent,'AUX9');
 assert.match(textOf(f.elements.txFields),/maximum configured power/i);await f.elements.rxRefresh.onclick();stage(f,'rxFields','Incoming RC channel',4);stage(f,'txFields','TX Power',2);
 const before=f.calls.length;await f.elements.txSave.onclick();assert(f.calls.slice(before).filter(c=>c.action==='write').every(c=>c.device==='238'),'TX Save cannot write pending RX edits');
 assert.equal(Number(inputFor(f,'rxFields','Incoming RC channel').value),4,'saving another section preserves pending receiver input');
 assert.equal(inputFor(f,'txFields','Dynamic power').options[2].textContent,'AUX5','one section refresh updates dependent choices');
 const reads=f.calls.slice(before).filter(c=>c.action==='read'&&c.device==='238');assert.equal(reads.length,4,'one full refresh per section, not per saved field');
 await f.elements.rxSave.onclick();const start=f.calls.length;await f.elements.review.onclick();assert(f.calls.slice(start).some(c=>c.path==='/remote/review'));
}

async function receiverContextAndCommands(){
 const f=fixture();await settle();await f.elements.txRefresh.onclick();await f.elements.rxRefresh.onclick();
 const output=inputFor(f,'rxFields','Physical PWM output'),oldInput=inputFor(f,'rxFields','Incoming RC channel');
 assert.equal(output.tag,'select');assert.equal(output.options[1].textContent,'Output 2');assert.equal(oldInput.options[2].textContent,'CH3');
 assert.match(textOf(f.elements.rxFields),/all configured PWM outputs/i);output.value='2';await output.onchange();
 assert.equal(inputFor(f,'rxFields','Incoming RC channel').value,8);assert.equal(inputFor(f,'txFields','TX Power'),undefined);
 oldInput.value='4';oldInput.oninput();const before=f.calls.length;await f.elements.rxSave.onclick();assert(!f.calls.slice(before).some(c=>c.action==='write'),'stale input context cannot be saved');
 await f.elements.rxRefresh.onclick();f.radio.confirm=false;await buttonFor(f,'rxFields','Capture failsafe positions').onclick();assert(!f.calls.some(c=>c.action==='command'));
 f.radio.confirm=true;await buttonFor(f,'rxFields','Capture failsafe positions').onclick();assert.equal(f.elements.confirm.hidden,false);assert.equal(f.elements.cancel.hidden,false);
 await f.elements.cancel.onclick();assert(f.calls.some(c=>c.action==='command'&&c.status==='5'&&c.device==='236'&&c.generation==='9'&&c.operationId));
 await f.elements.rxRefresh.onclick();await buttonFor(f,'rxFields','Capture failsafe positions').onclick();await f.elements.confirm.onclick();assert(f.calls.some(c=>c.action==='command'&&c.status==='4'&&c.confirmed===undefined));
 const held=fixture();await settle();await held.elements.rxRefresh.onclick();stage(held,'rxFields','Incoming RC channel',4);held.radio.confirm=false;
 const selector=inputFor(held,'rxFields','Physical PWM output');selector.value='2';await selector.onchange();assert.equal(selector.value,1);assert.equal(held.radio.appliedWrites.length,0,'output selection cannot discard drafts without consent');
}

async function commandOwnershipRecovery(){
 const busy=fixture();await settle();await busy.elements.rxRefresh.onclick();await buttonFor(busy,'rxFields','Capture failsafe positions').onclick();
 assert.equal(busy.elements.confirm.hidden,false);await busy.elements.status.onclick();
 assert.equal(busy.elements.confirm.hidden,false,'busy status rejection retains command confirmation');
 await busy.elements.confirm.onclick();assert(busy.calls.some(c=>c.path==='/remote/action'&&c.action==='command'&&c.status==='4'),'confirmation still uses the owned operation after status rejection');
 for(const status of [4,5]){
  const f=fixture();await settle();await f.elements.rxRefresh.onclick();await buttonFor(f,'rxFields','Capture failsafe positions').onclick();
  f.radio.loseCommandReplyStatus=status;const control=status===4?f.elements.confirm:f.elements.cancel;await control.onclick();
  assert.equal(f.calls.filter(c=>c.path==='/remote/action'&&c.action==='command'&&c.status===String(status)).length,1,'ambiguous command POST is never replayed');
  assert.equal(f.elements.confirm.hidden,true,'reconciled command state hides confirmation');
  assert.equal(f.elements.cancel.hidden,true,'reconciled command state hides cancel control after completion');
  assert(f.calls.filter(c=>c.path==='/remote/operation').length>=2,'ambiguous command POST is reconciled with operation GET');
 }
 const lostReply=fixture();await settle();await lostReply.elements.rxRefresh.onclick();await buttonFor(lostReply,'rxFields','Capture failsafe positions').onclick();
 lostReply.radio.loseCommandReplyStatus=4;lostReply.radio.failGetPath='/remote/operation';await lostReply.elements.confirm.onclick();
 assert.equal(lostReply.elements.confirm.hidden,true,'unreconciled ambiguous confirm hides confirmation control');
 assert.equal(lostReply.elements.cancel.hidden,true,'unreconciled ambiguous confirm hides cancel control');
 const queued=fixture();await settle();await queued.elements.rxRefresh.onclick();await buttonFor(queued,'rxFields','Capture failsafe positions').onclick();
 queued.radio.failGetPath='/remote/operation';await queued.elements.confirm.onclick();
 assert.equal(queued.elements.confirm.hidden,true,'accepted confirm with lost poll clears stale confirmation control');
 assert.equal(queued.elements.cancel.hidden,true,'accepted confirm with lost poll clears stale cancel control');
 const mismatch=fixture();await settle();await mismatch.elements.rxRefresh.onclick();await buttonFor(mismatch,'rxFields','Capture failsafe positions').onclick();
 mismatch.radio.mismatchOperationId=999;await mismatch.elements.confirm.onclick();
 assert.equal(mismatch.elements.confirm.hidden,true,'mismatched operation ID hides confirmation');
 assert.equal(mismatch.elements.cancel.hidden,true,'mismatched operation ID hides cancel');
 await mismatch.elements.confirm.onclick();assert.match(mismatch.elements.message.textContent,/ownership changed|refresh\/readback/i,'stale command handler is null-guarded');
 const ambiguousMismatch=fixture();await settle();await ambiguousMismatch.elements.rxRefresh.onclick();await buttonFor(ambiguousMismatch,'rxFields','Capture failsafe positions').onclick();
 ambiguousMismatch.radio.loseCommandReplyStatus=4;ambiguousMismatch.radio.mismatchOperationId=999;await ambiguousMismatch.elements.confirm.onclick();
 assert.equal(ambiguousMismatch.elements.confirm.hidden,true,'ambiguous response with mismatched ID hides confirmation');
 assert.equal(ambiguousMismatch.elements.cancel.hidden,true,'ambiguous response with mismatched ID hides cancel');
}
async function truthfulFailuresAndSupport(){
 for(const changed of [{hidden:true},{writable:false},{type:99}]){const f=fixture({transform:params=>Object.assign(params.find(p=>p.role==='power'),changed)});await settle();await f.elements.txRefresh.onclick();assert.equal(inputFor(f,'txFields','TX Power'),undefined);}
 const missing=fixture({transform:params=>params.find(p=>p.role==='dynamic').role=''});await settle();await missing.elements.txRefresh.onclick();assert.match(textOf(missing.elements.txFields),/Dynamic power.*unavailable/i);
 const midRead=fixture({transform:(_params,radio)=>radio.advanceOnRead='238:2'});await settle();await midRead.elements.txRefresh.onclick();assert.equal(inputFor(midRead,'txFields','TX Power'),undefined);
 const f=fixture();await settle();await f.elements.txRefresh.onclick();f.radio.failWrite=true;stage(f,'txFields','TX Power',2);await f.elements.txSave.onclick();assert.match(f.elements.message.textContent,/unknown/i);assert.equal(inputFor(f,'txFields','TX Power'),undefined);
 for(const changed of [{type:0},{writable:false},{hash:999}]){const stale=fixture();await settle();await stale.elements.txRefresh.onclick();stage(stale,'txFields','TX Power',2);stale.radio.changedPreview=changed;await stale.elements.txSave.onclick();assert.equal(stale.radio.appliedWrites.length,0,'backend must reject changed field before mutation');}
 const partial=fixture();await settle();partial.radio.failRead='238:4';await partial.elements.txRefresh.onclick();assert(inputFor(partial,'txFields','TX Power'));assert.equal(inputFor(partial,'txFields','Packet Rate'),undefined);assert.doesNotMatch(partial.elements.message.textContent,/verified/i);
 const missingReview=fixture({transform:params=>params.find(p=>p.role==='dynamic').role=''});await settle();await missingReview.elements.review.onclick();assert(!missingReview.calls.some(c=>c.path==='/remote/review'));
 const rx=fixture();await settle();await rx.elements.rxRefresh.onclick();rx.radio.failRead='236:2';const selector=inputFor(rx,'rxFields','Physical PWM output');selector.value='2';await selector.onchange();assert.equal(inputFor(rx,'rxFields','Incoming RC channel'),undefined);assert.doesNotMatch(rx.elements.message.textContent,/Readback verified/i);
}

async function genericTextReadback(){
 const f=fixture({transform:params=>{Object.assign(params.find(p=>p.device===236&&p.field===4),{name:'Model Id',type:12,info:'15',value:0});Object.assign(params.find(p=>p.device===236&&p.field===5),{name:'Firmware',type:10,info:'ELRS 3.5.6',value:0,hidden:false,writable:false});}});await settle();await f.elements.rxRefresh.onclick();
 for(const[name,value]of[['Model Id','15'],['Firmware','ELRS 3.5.6']]){const row=f.elements.rxFields.children.find(n=>n.children[0]?.textContent===name);assert.match(textOf(row),new RegExp('Verified value: '+value));assert(!descendants(row).some(n=>['input','select'].includes(n.tag)));}
 assert.match(textOf(f.elements.rxFields),/Capture PWM positions/);
}

async function filesAndFooter(){
 for(const localAP of [true,false])for(const crsfActive of [true,false]){
  const f=fixture({localAP,crsfActive});await settle();
  for(const id of ['backup','model','file','kind','preview','review'])assert.equal(f.elements[id].disabled,false,id+' works over AP or LAN without password');
  assert.equal(f.elements.apply.disabled,true,'activation requires an exact preview');
  await f.elements.backup.onclick();await f.elements.model.onclick();assert.equal(f.calls.filter(c=>c.path==='/remote/export'&&c.headers['X-Remote-Nonce']).length,2,'downloads retain the session nonce');
  f.elements.file.files=[new Blob(['{}'])];f.elements.kind.value='backup';await f.elements.preview.onclick();
  assert.equal(f.elements.apply.disabled,false);await f.elements.apply.onclick();
  assert(f.calls.some(c=>c.path==='/remote/upload'&&c.headers['X-Remote-Nonce']));
  assert(f.calls.some(c=>c.path==='/remote/apply'&&c.fileHash==='b'.repeat(64)&&c.confirmed==='1'));
  assert.equal(f.elements.apply.disabled,true);
  await f.elements.review.onclick();
  assert(f.calls.some(c=>c.path==='/remote/review'));
  if(!crsfActive)assert(!f.calls.some(c=>c.action==='status'),'inactive radio file operations skip RF status');
 }
 for(const formAction of ['param3save','/param3save']){
  const f=fixture({formAction});await settle();
  assert.equal(f.rootNode.parentNode,f.body);assert.equal(f.form.id,'elrs-settings');
  assert.equal(f.save.parentNode,f.body);assert.equal(f.save.getAttribute('form'),'elrs-settings');
  assert(f.body.children.indexOf(f.save)>f.body.children.indexOf(f.rootNode));
 }
 const general=fixture({crsfActive:false,formAction:'/paramsave'});await settle();
 assert.equal(general.save.parentNode,general.form);assert.equal(general.save.getAttribute('form'),null);
}
(async()=>{
 await sectionSavesAndWholePage();await blankNumbersAreInvalid();await generationAndProfileSaves();await sessionRenewal();await transientReadsAndFreshStatus();await liveControls();await receiverContextAndCommands();await commandOwnershipRecovery();await truthfulFailuresAndSupport();await genericTextReadback();await filesAndFooter();
 portalLayout(html);assert(embedded.endsWith(script),'gzip JavaScript differs from source');
 assert([...uiHeader.matchAll(/0x([0-9a-f]{2})/gi)].length<=8192,'UI exceeds 8 KiB gzip budget');
 console.log('TX ownership/readback, RX output generations, failsafe confirmation/cancel, unavailable controls, AP/LAN files, section/page saves, inline reads, footer and compressed parity PASS');
})().catch(e=>{console.error(e);process.exitCode=1;});
