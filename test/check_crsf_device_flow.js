'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm'),zlib=require('node:zlib');
const script=fs.readFileSync(require('node:path').join(__dirname,'../src/remote_config.js'),'utf8');
const uiHeader=fs.readFileSync(require('node:path').join(__dirname,'../src/remote_config_ui.h'),'utf8');
const uiBytes=[...uiHeader.matchAll(/0x([0-9a-f]{2})/gi)].map(m=>parseInt(m[1],16));
const embeddedScript=zlib.gunzipSync(Buffer.from(uiBytes)).toString('utf8');
function fixture(crsfActive,embedded=false,formId='',formAction='/param3save'){
 const elements={},calls=[],timers=[];let operation={state:'complete'},running=false;
 class Element{
  constructor(tag='div'){this.tag=tag;this.children=[];this.options=[];this.value='';this.dataset={};this.textContent='';this.files=[];this.hidden=false;this.parentNode=null;this.attributes={};}
  add(option){this.options.push(option);if(this.options.length===1)this.value=option.value;}
  get selectedOptions(){return this.options.filter(o=>o.value===this.value);}
  replaceChildren(){this.children=[];this.options=[];this.value='';}
  append(...nodes){for(const node of nodes){if(node.parentNode)node.parentNode.children.splice(node.parentNode.children.indexOf(node),1);node.parentNode=this;this.children.push(node);}}
  after(...nodes){if(!this.parentNode)return;const parent=this.parentNode;let index=parent.children.indexOf(this)+1;for(const node of nodes){if(node.parentNode){const oldIndex=node.parentNode.children.indexOf(node);node.parentNode.children.splice(oldIndex,1);if(node.parentNode===parent&&oldIndex<index)index--;}node.parentNode=parent;parent.children.splice(index++,0,node);}}
  closest(selector){for(let node=this;node;node=node.parentNode)if(selector==='form'&&node.tag==='form')return node;return null;}
  querySelector(selector){if(!/^button\[type=(?:['\"]submit['\"]|submit)\]$/.test(selector))return null;const visit=node=>{for(const child of node.children){if(child.tag==='button'&&child.type==='submit')return child;const found=visit(child);if(found)return found;}return null;};return visit(this);}
  setAttribute(name,value){this.attributes[name]=String(value);if(name==='id')this.id=String(value);}
  getAttribute(name){return this.attributes[name]??null;}
  focus(){document.activeElement=this;}
 }
 const body=new Element('body'),form=new Element('form'),root=new Element('div'),nativeSave=new Element('button');
 form.id=formId;form.setAttribute('action',formAction);root.id='remote-controls';nativeSave.type='submit';body.append(form);form.append(new Element('input'),root,nativeSave);
 const document={activeElement:null,getElementById:id=>elements[id]??=(new Element()),createElement:tag=>new Element(tag)};
 elements['remote-controls']=root;
 for(const id of ['access','message','maintenance','status','discover','selectModel','assign','backup','model','preview','review','apply','kind','file','clone','previewText','safety','devices','fields','field','modelId','confirm','cancel']){
  const node=new Element(id==='file'?'input':'div');elements[id]=node;root.append(node);
 }
 elements.modelId.value='0';
 class Option{constructor(label,value){this.textContent=label;this.value=String(value);this.dataset={};}}
 const parameter={device:238,field:1,generation:1,type:13,name:'Bind',commandAllowed:true,info:'Explicit command',unit:''};
 async function fetch(url,options={}){
  const path=url.split('?')[0],values=options.body instanceof URLSearchParams?Object.fromEntries(options.body):{};
  calls.push({path,...values});let data={},ok=true;
  if(path==='/remote/session')data={nonce:'a'.repeat(32)};
  else if(path==='/remote/devices')data={crsfActive,devices:crsfActive?[{address:238,name:'TX',fieldCount:1,generation:1}]:[],...(crsfActive?{rfKnown:true,connected:false,armedKnown:true,armed:false,ch5:172}:{})};
  else if(path==='/remote/upload')data={fileHash:'b'.repeat(64),credentials:'Target retained'};
  else if(path==='/remote/action'){
   if(values.action==='status'&&!crsfActive){ok=false;data={code:'UNSUPPORTED_BEHAVIOR',message:'CRSF inactive'};}
   else{if(values.action==='command')running=Number(values.status)!==5;operation={id:7,device:238,field:1,generation:1,state:running?'running':values.action==='command'?'cancelled':'complete',parameter:values.action==='read'?parameter:undefined};data={id:7,state:'queued'};}
  }else if(path==='/remote/operation')data=operation;
  return {ok,json:async()=>structuredClone(data)};
 }
 vm.runInNewContext(embedded?embeddedScript:script,{document,module:{exports:{}},Option,fetch,URLSearchParams,FormData,confirm:()=>true,URL,setTimeout:fn=>{timers.push(fn);return timers.length;} });
 return {elements,document,calls,timers,body,form,root,nativeSave};
}
const settle=()=>new Promise(setImmediate);
async function settingsSaveAtPageEnd(){
 for(const [existingId,expectedId] of [['','elrs-settings'],['crsf-settings','crsf-settings']]){
  const f=fixture(true,true,existingId);await settle();
  await f.elements.discover.onclick();
  const start=f.elements.field.children.find(e=>e.tag==='button');assert(start);
  assert.equal(f.root.parentNode,f.body,'live/file controls should be lifted out of the native form');
  assert.equal(f.root.closest('form'),null,'live controls must remain outside the native form');
  assert.equal(f.form.id,expectedId,'the external Save button needs a stable native form id');
  assert.equal(f.nativeSave.getAttribute('form'),f.form.id,'Save must still submit the native form');
  assert(f.body.children.indexOf(f.nativeSave)>f.body.children.indexOf(f.root),'native Save must follow all live and file controls');
  assert.equal(f.nativeSave.parentNode,f.body,'Save should sit after the full live/file panel');
  assert.equal(start.closest('form'),null,'live command widgets must remain outside the submitted form');
  assert.equal(f.elements.file.closest('form'),null,'file controls must remain outside the submitted form');
 }
 const general=fixture(false,true,'','/paramsave');await settle();
 assert.equal(general.root.parentNode,general.body,'general page controls should still be lifted out of the form');
 assert.equal(general.root.closest('form'),null);
 assert.equal(general.nativeSave.parentNode,general.form,'the General Settings Save must remain inside its original form');
 assert.equal(general.nativeSave.getAttribute('form'),null,'General Settings Save must not be rebound or moved');
}
async function nonCrsfBackupAndReview(){
 const f=fixture(false);await settle();
 assert(!f.elements.safety.textContent.includes('undefined'),'inactive radio metadata rendered undefined values');
 f.document.getElementById('kind').value='backup';f.document.getElementById('file').files=[new File(['{}'],'backup.remote-backup.json')];
 await f.elements.preview.onclick();await f.elements.apply.onclick();
 assert(f.calls.some(c=>c.path==='/remote/apply'),'non-CRSF backup activation was blocked by a radio status request');
 await f.elements.review.onclick();
 assert(f.calls.some(c=>c.path==='/remote/review'),'non-CRSF review was blocked by a radio status request');
 assert(!f.calls.some(c=>c.action==='status'));
}
async function runningCommandCancel(){
 const f=fixture(true);await settle();await f.elements.discover.onclick();
 const start=f.elements.field.children.find(e=>e.tag==='button');assert(start);
 assert.equal(start.type,'button','live command must not submit the ELRS settings form');
 const command=start.onclick();await settle();assert.equal(f.elements.cancel.hidden,false);
 await f.elements.cancel.onclick();
 assert(f.calls.some(c=>c.action==='command'&&Number(c.status)===5),'running Cancel did not send the owned cancellation');
 const resumed=f.timers.splice(0);for(const resume of resumed)resume();await command;
 assert.equal(f.calls.filter(c=>c.action==='command'&&Number(c.status)===1).length,1);
 assert.equal(f.calls.filter(c=>c.action==='command'&&Number(c.status)===5).length,1);
}
(async()=>{let failures=0;for(const test of [settingsSaveAtPageEnd,nonCrsfBackupAndReview,runningCommandCancel]){try{await test();console.log(test.name+' PASS');}catch(e){failures++;console.error(test.name+': '+e.message);}}process.exitCode=failures?1:0;})();
