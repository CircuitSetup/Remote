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
 for(const section of ['Live ELRS controls','Radio devices','Transmitter','Receiver outputs','CRSF settings files','External radio settings']){
  const at=source.indexOf('<h3>'+section+'</h3>');assert(at>last,section+' section is missing or out of order');last=at;
 }
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
function fixture({crsfActive=true,localAP=true,formAction='/param3save',absentTX=false,transform=()=>{},fieldCount=4,operationAdvanceMs=0,failGetOnce='',staleDeviceStatus=false,rejectStatus=false}={}){
 const elements={},calls=[],timers=[],confirmations=[];
 let clock=0,sessionNonce='',sessionExpires=0;
 const body=new Element('body'),form=new Element('form'),rootNode=new Element('div'),save=new Element('button');
 form.action=formAction;rootNode.id='remote-controls';save.type='submit';body.append(form);form.append(rootNode,save);
 const document={readyState:'complete',getElementById:id=>elements[id]??=(new Element()),createElement:tag=>new Element(tag)};
 elements['remote-controls']=rootNode;
 for(const id of ['access','message','maintenance','status','discover','selectModel','assign','backup','model','preview','review','apply','kind','file','previewText','safety','devices','fields','field','modelId','confirm','cancel','txRefresh','txFields','rxRefresh','rxFields'])elements[id]=new Element(id==='file'?'input':id.endsWith('Fields')?'div':id==='devices'||id==='fields'?'select':'button');
 elements.modelId.value='0';
  const radio={txGeneration:3,rxGeneration:7,txPresent:!absentTX,failRead:null,failReadOnce:null,failWrite:false,failPostAction:null,loseCommandReplyStatus:null,failGetPath:null,mismatchOperationId:null,changedPreview:null,throttleReady:false,confirm:true};
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
   if(pathname==='/remote/devices')data={crsfActive,localAP,throttleReady:radio.throttleReady,rfKnown:true,connected:true,modelMatched:true,armedKnown:true,armed:false,ch5:172,statusAgeMs:staleDeviceStatus?5147:10,devices:!crsfActive?[]:[...(radio.txPresent?[{address:238,name:'TX',fieldCount,generation:radio.txGeneration}]:[]),{address:236,name:'RX',fieldCount:5,generation:radio.rxGeneration}]};
  else if(pathname==='/remote/upload')data={fileHash:'b'.repeat(64),calibration:"Keep this Remote's calibration"};
  else if(pathname==='/remote/apply'||pathname==='/remote/review')data={ok:true};
  else if(pathname==='/remote/action'&&values.action==='status'&&(rejectStatus||current.state==='confirmation')){ok=false;data={code:rejectStatus?'UNSUPPORTED_BEHAVIOR':'BUSY',path:'radio',message:rejectStatus?'RF status is unavailable for this transmitter':'Command confirmation is pending'};}
  else if(pathname==='/remote/action'){
   const device=Number(values.device),field=Number(values.field),p=params.find(p=>p.device===device&&p.field===field);
    let loseCommandReply=false;
    current={id:values.operationId?Number(values.operationId):++sequence,state:'complete',action:values.action,device,field,generation:generation(device)};
   if(values.action==='discover')radio.txPresent=true;
   if(['read','write','command'].includes(values.action)){
    if(Number(values.generation)!==generation(device)){current.state='failed';current.message='Stale generation';}
    else if(!p){current.state='failed';current.message='Missing field';}
     else if((radio.failRead===device+':'+field||radio.failReadOnce===device+':'+field)&&values.action==='read'){if(radio.failReadOnce===device+':'+field)radio.failReadOnce=null;current.state='failed';current.error='timeout';current.message='Read timeout';}
    else{
     if(values.action==='read'&&radio.changedPreview)Object.assign(p,radio.changedPreview);
     if(values.action==='write'){
      if(radio.failWrite){current.state='unknown';current.message='Write outcome unknown';}
      else{p.value=Number(values.value);if(p.name==='Output Ch'||p.role==='rfRate'){radio.txGeneration=radio.rxGeneration=9;params.find(p=>p.device===236&&p.name==='Input Ch').value=8;}if(p.role==='power')params.find(p=>p.role==='dynamic').options='Off;On;AUX5;AUX9';}
     }
      if(values.action==='command'){current.state=Number(values.status)===1?'confirmation':Number(values.status)===5?'cancelled':'complete';if(radio.loseCommandReplyStatus===Number(values.status)){radio.loseCommandReplyStatus=null;loseCommandReply=true;}}
     current.generation=generation(device);current.parameter={...p,generation:generation(device)};if(values.action==='read'&&radio.advanceOnRead===device+':'+field)radio.txGeneration=4;
    }
   }
   data={id:current.id,state:'queued'};
    if(loseCommandReply)throw Error('ambiguous command POST');
   }else if(pathname==='/remote/operation'){data={...current,...(current.action==='status'?{radioStatus:{crsfActive,rfKnown:true,connected:true,modelMatched:true,armedKnown:true,armed:false,statusAgeMs:12,ch5:172,throttleReady:radio.throttleReady}}:{})};if(radio.mismatchOperationId!==null){data.id=radio.mismatchOperationId;radio.mismatchOperationId=null;}clock+=operationAdvanceMs;}
  return {ok,json:async()=>structuredClone(data),blob:async()=>new Blob(['{}'])};
 }
 vm.runInNewContext(embedded,{document,module:{exports:{}},Option,fetch,URLSearchParams,FormData,confirm:message=>{confirmations.push(message);return radio.confirm;},URL:{createObjectURL:()=>'',revokeObjectURL:()=>{}},setTimeout:fn=>{timers.push(fn);return timers.length;}});
 return {elements,calls,timers,document,rootNode,body,form,save,radio,params,confirmations,advanceTime:ms=>{clock+=ms;},elapsed:()=>clock,replaceRemoteSession:()=>{sessionNonce='b'.repeat(32);sessionExpires=clock+300000;}};
}
const settle=()=>new Promise(setImmediate);
async function editorTargetsAndApply(){
 const f=fixture();await settle();
 assert.equal(f.elements.txDevice?.textContent,'Device: TX · 238','transmitter section identifies its own target');
 assert.equal(f.elements.rxDevice?.textContent,'Device: RX · 236','receiver section identifies its own target');
 await f.elements.txRefresh.onclick();
 const apply=buttonFor(f,'txFields','Apply TX Power');assert(apply,'write button says Apply rather than Preview');
 inputFor(f,'txFields','TX Power').value='2';
 assert(!f.calls.some(c=>c.action==='write'),'choosing a value does not write to the radio');
 f.radio.confirm=false;await apply.onclick();
 assert(!f.calls.some(c=>c.action==='write'),'cancelling confirmation does not write to the radio');
 assert.match(f.confirmations.at(-1),/Apply TX Power on TX · 238/,'confirmation names the target device');
 f.elements.devices.value='236';await f.elements.devices.onchange();
 assert.equal(f.elements.fieldDevice.textContent,'Device: RX · 236','generic editor follows the selected device');
 assert.equal(f.elements.txDevice.textContent,'Device: TX · 238','generic selection does not relabel transmitter controls');
 inputFor(f,'txFields','TX Power').value='2';
 f.radio.confirm=true;await buttonFor(f,'txFields','Apply TX Power').onclick();
 assert(f.calls.some(c=>c.action==='write'&&c.device==='238'&&c.field==='2'&&c.value==='2'),'Apply writes the named transmitter field');
 const absent=fixture({absentTX:true});await settle();
 assert.equal(absent.elements.txDevice.textContent,'Device: Not discovered','missing device identity is explicit');
}
async function transientReadsAndFreshStatus(){
 const retry=fixture({failGetOnce:'/remote/operation'});await settle();await retry.elements.txRefresh.onclick();
 assert(inputFor(retry,'txFields','TX Power'),'a transient operation GET failure recovers the readback');
 assert.equal(retry.calls.filter(c=>c.path==='/remote/action'&&c.action==='read'&&c.field==='2').length,1,'operation polling retries without reposting the read action');
 assert(retry.calls.filter(c=>c.path==='/remote/operation').length>retry.calls.filter(c=>c.path==='/remote/action').length,'bounded GET retry repeats the operation poll');
 const devicesRetry=fixture({failGetOnce:'/remote/devices'});await settle();await devicesRetry.elements.txRefresh.onclick();
 assert(inputFor(devicesRetry,'txFields','TX Power'),'a transient devices GET failure recovers refresh');
 const readRetry=fixture();await settle();readRetry.radio.failReadOnce='238:2';await readRetry.elements.txRefresh.onclick();
 assert(inputFor(readRetry,'txFields','TX Power'),'a terminal failed field read is retried once after matching operation reconciliation');
 assert.equal(readRetry.calls.filter(c=>c.path==='/remote/action'&&c.action==='read'&&c.field==='2').length,2,'only the matching failed read is retried');
 const staleTimeout=fixture();await settle();await staleTimeout.elements.txRefresh.onclick();const staleSave=buttonFor(staleTimeout,'txFields','Apply TX Power');
 staleTimeout.radio.failRead='238:2';await staleSave.onclick();staleTimeout.radio.failRead=null;staleTimeout.radio.failPostAction='read';
 const priorReads=staleTimeout.calls.filter(c=>c.path==='/remote/action'&&c.action==='read'&&c.field==='2').length;await staleSave.onclick();
 assert.equal(staleTimeout.calls.filter(c=>c.path==='/remote/action'&&c.action==='read'&&c.field==='2').length-priorReads,1,'a prior same-field timeout cannot replay an ambiguous read POST');
 const ambiguousRead=fixture();await settle();ambiguousRead.radio.failPostAction='read';await ambiguousRead.elements.txRefresh.onclick();
 assert.equal(ambiguousRead.calls.filter(c=>c.path==='/remote/action'&&c.action==='read'&&c.field==='1').length,1,'ambiguous read POST is not replayed');
 assert.equal(inputFor(ambiguousRead,'txFields','Telemetry Ratio'),undefined,'an unconfirmed ambiguous read remains unavailable');
 const ambiguous=fixture();await settle();await ambiguous.elements.txRefresh.onclick();
 assert.equal(ambiguous.calls.filter(c=>c.path==='/remote/action'&&c.action==='read').length,4);
 ambiguous.radio.failPostAction='write';inputFor(ambiguous,'txFields','TX Power').value='2';await buttonFor(ambiguous,'txFields','Apply TX Power').onclick();
 assert.equal(ambiguous.calls.filter(c=>c.path==='/remote/action'&&c.action==='write').length,1,'ambiguous write POST is never replayed');
 assert(!ambiguous.calls.some(c=>['command','upload','apply','review'].includes(c.action)||['/remote/upload','/remote/apply','/remote/review'].includes(c.path)),'read recovery never replays commands or file actions');
 assert.doesNotMatch(ambiguous.elements.message.textContent,/verified/i);
 const fresh=fixture({staleDeviceStatus:true});await settle();await fresh.elements.txRefresh.onclick();
 assert.match(fresh.elements.safety.textContent,/status age 12 ms/,'TX refresh displays status returned by its status operation');
 assert.doesNotMatch(fresh.elements.safety.textContent,/5147 ms/,'an older devices snapshot does not overwrite operation status');
 const unsupported=fixture({rejectStatus:true});await settle();await unsupported.elements.txRefresh.onclick();
 assert(inputFor(unsupported,'txFields','TX Power'),'status failure preserves verified field readback');
 assert.equal(inputFor(unsupported,'txFields','TX Power').value,1);
 assert.match(unsupported.elements.safety.textContent,/RF unknown.*arm state unknown.*Model Match unknown/i,'status failure clears stale safety values');
 assert.match(unsupported.elements.message.textContent,/RF status is unavailable/i,'status failure remains visible');
 assert(!unsupported.calls.some(c=>['write','command'].includes(c.action)),'status recovery does not mutate radio settings');
 const incomplete=fixture({rejectStatus:true});await settle();incomplete.radio.failRead='238:4';await incomplete.elements.txRefresh.onclick();
 assert.match(incomplete.elements.message.textContent,/Readback incomplete.*Field 4/i,'RF status failure preserves incomplete field errors');
 assert.match(incomplete.elements.safety.textContent,/RF unknown.*arm state unknown.*Model Match unknown/i);
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
 const staleMutation=fixture();await settle();await staleMutation.elements.txRefresh.onclick();inputFor(staleMutation,'txFields','TX Power').value='2';
 staleMutation.replaceRemoteSession();const mutationStart=staleMutation.calls.length;await buttonFor(staleMutation,'txFields','Apply TX Power').onclick();
 assert(!staleMutation.calls.slice(mutationStart).some(c=>c.path==='/remote/action'),'replaced session aborts stale radio mutation without replay');
 assert.match(staleMutation.elements.message.textContent,/session changed/i);
}
async function liveControls(){
 const f=fixture({absentTX:true});await settle();
 assert.equal(typeof f.elements.txRefresh.onclick,'function','dedicated TX refresh executes a production handler');
 await f.elements.txRefresh.onclick();
 assert(f.calls.some(c=>c.action==='discover'),'TX refresh discovers a missing transmitter');
 assert.match(f.elements.safety.textContent,/calibrated center/i);
 assert.equal(inputFor(f,'txFields','Telemetry Ratio').value,2);
 assert.equal(inputFor(f,'txFields','Telemetry Ratio').options[1].disabled,true);
 assert.equal(inputFor(f,'txFields','Packet Rate').options[1].textContent,'333Hz Full');
 assert.equal(inputFor(f,'txFields','Dynamic power').options[3].textContent,'AUX9');
 assert.match(textOf(f.elements.txFields),/maximum configured power/i);
 f.elements.devices.value='236';await f.elements.devices.onchange();
 const power=inputFor(f,'txFields','TX Power');power.value='2';const before=f.calls.length;
 await buttonFor(f,'txFields','Apply TX Power').onclick();
 const actions=f.calls.slice(before).filter(c=>['read','write'].includes(c.action));
 assert(actions.every(c=>c.device==='238'&&c.generation==='3'),'selected RX must not redirect TX or its generation');
 assert.equal(actions.find(c=>c.action==='write').confirmed,'1');
 assert.equal(inputFor(f,'txFields','TX Power').value,2);
 assert.equal(inputFor(f,'txFields','Dynamic power').options[2].textContent,'AUX5','power changes refresh dependent TX choices');
 await f.elements.selectModel.onclick();
 assert.equal(f.calls.find(c=>c.action==='model').generation,'3','TX profile uses TX generation with RX selected');
 const start=f.calls.length;await f.elements.review.onclick();
 const reviewReads=f.calls.slice(start).filter(c=>c.action==='read');
 for(const field of [1,2,3])assert(reviewReads.some(c=>c.device==='238'&&c.field===String(field)),'radio review reads TX descriptors with RX selected');
 assert(f.calls.slice(start).some(c=>c.path==='/remote/review'));
}
async function receiverContextAndCommands(){
 const f=fixture();await settle();await f.elements.txRefresh.onclick();await f.elements.rxRefresh.onclick();
 const output=inputFor(f,'rxFields','Physical PWM output'),input=inputFor(f,'rxFields','Incoming RC channel');
 assert.equal(output.tag,'select');assert.equal(output.options[1].textContent,'Output 2');
 assert.equal(input.tag,'select');assert.equal(input.options[2].textContent,'CH3');
 assert.match(textOf(f.elements.rxFields),/all configured PWM outputs/i);
 const stale=buttonFor(f,'rxFields','Apply Incoming RC channel');
 output.value='2';const before=f.calls.length;
 await buttonFor(f,'rxFields','Apply Physical PWM output').onclick();
 const actions=f.calls.slice(before),write=actions.findIndex(c=>c.action==='write');
 assert(actions.slice(write+1).some(c=>c.action==='read'&&c.device==='236'&&c.field==='2'&&c.generation==='9'),'Output Ch rereads Input Ch at the new generation');
 assert.equal(inputFor(f,'rxFields','Incoming RC channel').value,8);assert.equal(inputFor(f,'txFields','TX Power'),undefined,'RX generation changes invalidate cached TX values too');
 const after=f.calls.length;await stale.onclick();
 assert(!f.calls.slice(after).some(c=>c.action==='write'),'stale input handler cannot write a previous output context');
 assert.match(f.elements.message.textContent,/reread|refresh|changed|stale/i);
 await f.elements.rxRefresh.onclick();
 f.radio.confirm=false;await buttonFor(f,'rxFields','Capture failsafe positions').onclick();
 assert(!f.calls.some(c=>c.action==='command'),'capture requires explicit local confirmation');
 f.radio.confirm=true;await buttonFor(f,'rxFields','Capture failsafe positions').onclick();
 assert.equal(f.elements.confirm.hidden,false);assert.equal(f.elements.cancel.hidden,false);
 await f.elements.cancel.onclick();
 assert(f.calls.some(c=>c.action==='command'&&c.status==='5'&&c.device==='236'&&c.generation==='9'&&c.operationId),'cancel keeps command ownership');
 await f.elements.rxRefresh.onclick();await buttonFor(f,'rxFields','Capture failsafe positions').onclick();await f.elements.confirm.onclick();
 assert(f.calls.some(c=>c.action==='command'&&c.status==='4'&&c.confirmed===undefined));
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
  assert(f.calls.filter(c=>c.path==='/remote/operation').length>=3,'ambiguous command POST is reconciled with operation GET');
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
 for(const changed of [{hidden:true},{writable:false},{type:99}]){
  const f=fixture({transform:params=>Object.assign(params.find(p=>p.role==='power'),changed)});await settle();await f.elements.txRefresh.onclick();
  assert.equal(inputFor(f,'txFields','TX Power'),undefined,'hidden/read-only/unsupported controls cannot edit');
  assert.match(textOf(f.elements.txFields),/unavailable|unsupported|read.only/i);
 }
 const missing=fixture({transform:params=>params.find(p=>p.role==='dynamic').role=''});await settle();await missing.elements.txRefresh.onclick();
 assert.match(textOf(missing.elements.txFields),/Dynamic power.*unavailable/i);
 const midRead=fixture({transform:(_params,radio)=>radio.advanceOnRead='238:2'});await settle();await midRead.elements.txRefresh.onclick();
 assert.equal(inputFor(midRead,'txFields','TX Power'),undefined,'generation advancing during menu reads cannot leave prior live labels verified');
 const changedGeneration=fixture();await settle();await changedGeneration.elements.txRefresh.onclick();changedGeneration.radio.txGeneration=4;await changedGeneration.elements.status.onclick();
 assert.equal(inputFor(changedGeneration,'txFields','TX Power'),undefined,'public device generation mismatch clears verified TX labels');
 const f=fixture();await settle();await f.elements.txRefresh.onclick();f.radio.failWrite=true;inputFor(f,'txFields','TX Power').value='2';
 await buttonFor(f,'txFields','Apply TX Power').onclick();
 assert.match(f.elements.message.textContent,/unknown/i);
 assert.equal(inputFor(f,'txFields','TX Power'),undefined,'failed writes require refresh, not stale verified values');
 f.radio.failWrite=false;await f.elements.txRefresh.onclick();f.radio.changedPreview={type:0};
 await buttonFor(f,'txFields','Apply TX Power').onclick();
 assert(!f.calls.some(c=>c.action==='write'&&c.type==='0'),'descriptor type must match preview before mutation');
  const writable=fixture();await settle();await writable.elements.txRefresh.onclick();writable.radio.changedPreview={writable:false};
 await buttonFor(writable,'txFields','Apply TX Power').onclick();
 assert(!writable.calls.some(c=>c.action==='write'),'a now read-only descriptor must not mutate');
 const partial=fixture();await settle();partial.radio.failRead='238:4';await partial.elements.txRefresh.onclick();
 assert(inputFor(partial,'txFields','TX Power'),'one unreadable descriptor must not hide other fresh fields');
 assert.equal(inputFor(partial,'txFields','Packet Rate'),undefined);
 assert.doesNotMatch(partial.elements.message.textContent,/verified/i);
 const missingReview=fixture({transform:params=>params.find(p=>p.role==='dynamic').role=''});await settle();await missingReview.elements.review.onclick();
 assert(!missingReview.calls.some(c=>c.path==='/remote/review'),'missing TX readback cannot claim imported review is verified');
 const rx=fixture();await settle();await rx.elements.rxRefresh.onclick();rx.radio.failRead='236:2';inputFor(rx,'rxFields','Physical PWM output').value='2';
 await buttonFor(rx,'rxFields','Apply Physical PWM output').onclick();
 assert.equal(inputFor(rx,'rxFields','Incoming RC channel'),undefined,'timed out dependent read is unavailable');
 assert.doesNotMatch(rx.elements.message.textContent,/Readback verified/i);
}
async function genericTextReadback(){
 const f=fixture({transform:params=>{
  Object.assign(params.find(p=>p.device===236&&p.field===4),{name:'Model Id',type:12,info:'15',value:0});
  Object.assign(params.find(p=>p.device===236&&p.field===5),{name:'Firmware',type:10,info:'ELRS 3.5.6',value:0,hidden:false,writable:false});
 }});
 await settle();f.elements.devices.value='236';await f.elements.devices.onchange();
 for(const [field,actual]of [['4','15'],['5','ELRS 3.5.6']]){
  f.elements.fields.value=field;f.elements.fields.onchange();
  assert(textOf(f.elements.field).includes('Verified value: '+actual),'generic text/info readback must preserve '+actual);
  assert.doesNotMatch(textOf(f.elements.field),/Verified value: 0/);
  assert(!descendants(f.elements.field).some(n=>n.tag==='input'||n.tag==='select'),'text/info descriptors remain read-only');
 }
 assert.match(textOf(f.elements.rxFields),/Capture PWM positions/,'command info remains visible');
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
 await editorTargetsAndApply();await sessionRenewal();await transientReadsAndFreshStatus();await liveControls();await receiverContextAndCommands();await commandOwnershipRecovery();await truthfulFailuresAndSupport();await genericTextReadback();await filesAndFooter();
 portalLayout(html);assert(embedded.endsWith(script),'gzip JavaScript differs from source');
 assert([...uiHeader.matchAll(/0x([0-9a-f]{2})/gi)].length<=8192,'UI exceeds 8 KiB gzip budget');
 console.log('TX ownership/readback, RX output generations, failsafe confirmation/cancel, unavailable controls, AP/LAN files, footer and compressed parity PASS');
})().catch(e=>{console.error(e);process.exitCode=1;});
