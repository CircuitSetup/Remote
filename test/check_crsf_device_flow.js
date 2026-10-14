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
 for(const section of ['Live ELRS controls','Transmitter','Receiver outputs','Radio devices','CRSF settings files','External radio settings']){
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
function fixture({crsfActive=true,localAP=true,formAction='/param3save',absentTX=false,transform=()=>{}}={}){
 const elements={},calls=[],timers=[],confirmations=[];
 const body=new Element('body'),form=new Element('form'),rootNode=new Element('div'),save=new Element('button');
 form.action=formAction;rootNode.id='remote-controls';save.type='submit';body.append(form);form.append(rootNode,save);
 const document={readyState:'complete',getElementById:id=>elements[id]??=(new Element()),createElement:tag=>new Element(tag)};
 elements['remote-controls']=rootNode;
 for(const id of ['access','message','maintenance','status','discover','selectModel','assign','backup','model','preview','review','apply','kind','file','previewText','safety','devices','fields','field','modelId','confirm','cancel','txRefresh','txFields','rxRefresh','rxFields'])elements[id]=new Element(id==='file'?'input':id.endsWith('Fields')?'div':id==='devices'||id==='fields'?'select':'button');
 elements.modelId.value='0';
 const radio={txGeneration:3,rxGeneration:7,txPresent:!absentTX,failRead:null,failWrite:false,changedPreview:null,throttleReady:false,confirm:true};
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
 transform(params,radio);
 let current={state:'idle'},sequence=0;
 const generation=d=>d===238?radio.txGeneration:radio.rxGeneration;
 async function fetch(url,options={}){
  const pathname=url.split('?')[0],values=options.body instanceof URLSearchParams?Object.fromEntries(options.body):{};
  calls.push({path:pathname,url,method:options.method||'GET',headers:options.headers,...values});let data={},ok=true;
  if(pathname==='/remote/session')data={nonce:'a'.repeat(32)};
  else if(pathname==='/remote/devices')data={crsfActive,localAP,throttleReady:radio.throttleReady,rfKnown:true,connected:true,modelMatched:true,armedKnown:true,armed:false,ch5:172,statusAgeMs:10,devices:!crsfActive?[]:[...(radio.txPresent?[{address:238,name:'TX',fieldCount:4,generation:radio.txGeneration}]:[]),{address:236,name:'RX',fieldCount:5,generation:radio.rxGeneration}]};
  else if(pathname==='/remote/upload')data={fileHash:'b'.repeat(64),calibration:"Keep this Remote's calibration"};
  else if(pathname==='/remote/apply'||pathname==='/remote/review')data={ok:true};
  else if(pathname==='/remote/action'){
   const device=Number(values.device),field=Number(values.field),p=params.find(p=>p.device===device&&p.field===field);
   current={id:values.operationId?Number(values.operationId):++sequence,state:'complete',device,field,generation:generation(device)};
   if(values.action==='discover')radio.txPresent=true;
   if(['read','write','command'].includes(values.action)){
    if(Number(values.generation)!==generation(device)){current.state='failed';current.message='Stale generation';}
    else if(!p){current.state='failed';current.message='Missing field';}
    else if(radio.failRead===device+':'+field&&values.action==='read'){current.state='failed';current.message='Read timeout';}
    else{
     if(values.action==='read'&&radio.changedPreview)Object.assign(p,radio.changedPreview);
     if(values.action==='write'){
      if(radio.failWrite){current.state='unknown';current.message='Write outcome unknown';}
      else{p.value=Number(values.value);if(p.name==='Output Ch'||p.role==='rfRate'){radio.txGeneration=radio.rxGeneration=9;params.find(p=>p.device===236&&p.name==='Input Ch').value=8;}if(p.role==='power')params.find(p=>p.role==='dynamic').options='Off;On;AUX5;AUX9';}
     }
     if(values.action==='command')current.state=Number(values.status)===1?'confirmation':Number(values.status)===5?'cancelled':'complete';
     current.generation=generation(device);current.parameter={...p,generation:generation(device)};if(values.action==='read'&&radio.advanceOnRead===device+':'+field)radio.txGeneration=4;
    }
   }
   data={id:current.id,state:'queued'};
  }else if(pathname==='/remote/operation')data=current;
  return {ok,json:async()=>structuredClone(data),blob:async()=>new Blob(['{}'])};
 }
 vm.runInNewContext(embedded,{document,module:{exports:{}},Option,fetch,URLSearchParams,FormData,confirm:message=>{confirmations.push(message);return radio.confirm;},URL:{createObjectURL:()=>'',revokeObjectURL:()=>{}},setTimeout:fn=>{timers.push(fn);return timers.length;}});
 return {elements,calls,timers,document,rootNode,body,form,save,radio,params,confirmations};
}
const settle=()=>new Promise(setImmediate);
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
 await buttonFor(f,'txFields','Preview TX Power change').onclick();
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
 const stale=buttonFor(f,'rxFields','Preview Incoming RC channel change');
 output.value='2';const before=f.calls.length;
 await buttonFor(f,'rxFields','Preview Physical PWM output change').onclick();
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
 await buttonFor(f,'txFields','Preview TX Power change').onclick();
 assert.match(f.elements.message.textContent,/unknown/i);
 assert.equal(inputFor(f,'txFields','TX Power'),undefined,'failed writes require refresh, not stale verified values');
 f.radio.failWrite=false;await f.elements.txRefresh.onclick();f.radio.changedPreview={type:0};
 await buttonFor(f,'txFields','Preview TX Power change').onclick();
 assert(!f.calls.some(c=>c.action==='write'&&c.type==='0'),'descriptor type must match preview before mutation');
  const writable=fixture();await settle();await writable.elements.txRefresh.onclick();writable.radio.changedPreview={writable:false};
 await buttonFor(writable,'txFields','Preview TX Power change').onclick();
 assert(!writable.calls.some(c=>c.action==='write'),'a now read-only descriptor must not mutate');
 const partial=fixture();await settle();partial.radio.failRead='238:4';await partial.elements.txRefresh.onclick();
 assert(inputFor(partial,'txFields','TX Power'),'one unreadable descriptor must not hide other fresh fields');
 assert.equal(inputFor(partial,'txFields','Packet Rate'),undefined);
 assert.doesNotMatch(partial.elements.message.textContent,/verified/i);
 const missingReview=fixture({transform:params=>params.find(p=>p.role==='dynamic').role=''});await settle();await missingReview.elements.review.onclick();
 assert(!missingReview.calls.some(c=>c.path==='/remote/review'),'missing TX readback cannot claim imported review is verified');
 const rx=fixture();await settle();await rx.elements.rxRefresh.onclick();rx.radio.failRead='236:2';inputFor(rx,'rxFields','Physical PWM output').value='2';
 await buttonFor(rx,'rxFields','Preview Physical PWM output change').onclick();
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
 await liveControls();await receiverContextAndCommands();await truthfulFailuresAndSupport();await genericTextReadback();await filesAndFooter();
 portalLayout(html);assert(embedded.endsWith(script),'gzip JavaScript differs from source');
 assert([...uiHeader.matchAll(/0x([0-9a-f]{2})/gi)].length<=8192,'UI exceeds 8 KiB gzip budget');
 console.log('TX ownership/readback, RX output generations, failsafe confirmation/cancel, unavailable controls, AP/LAN files, footer and compressed parity PASS');
})().catch(e=>{console.error(e);process.exitCode=1;});
