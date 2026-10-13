'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),zlib=require('node:zlib');
const root=path.join(__dirname,'../src/src/CRSF');
const script=fs.readFileSync(path.join(root,'crsf_config.js'),'utf8').replace(/\r\n/g,'\n');
const html=fs.readFileSync(path.join(root,'crsf_config.html'),'utf8');
const wifi=fs.readFileSync(path.join(root,'crsf_wifi.h'),'utf8');
const uiHeader=fs.readFileSync(path.join(root,'crsf_config_ui.h'),'utf8');
const embedded=zlib.gunzipSync(Buffer.from([...uiHeader.matchAll(/0x([0-9a-f]{2})/gi)].map(m=>parseInt(m[1],16)))).toString('utf8');

function portalLayout(source){
 const sections=['Live ELRS controls','Radio devices','CRSF settings files','External radio settings'];
 let last=-1;
 for(const section of sections){const at=source.indexOf('<h3>'+section+'</h3>');assert(at>last,section+' section is missing or out of order');last=at;}
 assert(source.indexOf('id="review"')>source.indexOf('<h3>External radio settings</h3>'));
 assert.match(wifi,/id='remote-controls'/);
 assert.match(wifi,/custom_remote_controls\("<div id='remote-controls'><\/div><script defer src='\/remote\/ui\.js'><\/script>"\)/);
 const parameters=wifi.slice(wifi.indexOf('WiFiManagerParameter * const crsfParmArray[]'),wifi.indexOf('NULL',wifi.indexOf('WiFiManagerParameter * const crsfParmArray[]')));
 assert(parameters.trimEnd().endsWith('&custom_remote_controls,'),'live/file/review controls must be the last CRSF form content before Save');
 assert(wifi.includes("<script defer src='/remote/ui.js'></script>"));
 assert.doesNotMatch(wifi,/WiFiManagerParameter custom_\w*(?:tlm|power|dynp)/i,'legacy top-level RF ratio/power controls returned');
 assert.doesNotMatch(html,/(?:ctlmr|cmpwr|cdynp|elrsTlmRatio|elrsMaxPower)/,'legacy radio form fields returned');
 assert.doesNotMatch(source,/<button(?![^>]*\btype=['"]button['"])/i,'portal controls must not accidentally submit the settings form');
}

class Element{
 constructor(tag='div'){this.tag=tag;this.children=[];this.options=[];this.value='';this.dataset={};this.textContent='';this.files=[];this.hidden=false;this.parentNode=null;this.attributes={};}
 add(node){this.options.push(node);if(this.options.length===1)this.value=node.value;}
 replaceChildren(...nodes){this.children=[];this.options=[];this.value='';for(const node of nodes)this.append(node);}
 append(...nodes){for(const node of nodes){if(node.parentNode){const list=node.parentNode.children,i=list.indexOf(node);if(i>=0)list.splice(i,1);}node.parentNode=this;this.children.push(node);}}
 after(...nodes){if(!this.parentNode)return;const parent=this.parentNode;let at=parent.children.indexOf(this)+1;for(const node of nodes){if(node.parentNode){const old=node.parentNode.children.indexOf(node);node.parentNode.children.splice(old,1);if(node.parentNode===parent&&old<at)at--;}node.parentNode=parent;parent.children.splice(at++,0,node);}}
 closest(selector){for(let n=this;n;n=n.parentNode)if(selector==='form'&&n.tag==='form')return n;return null;}
 querySelector(selector){return selector==='button[type=submit]'?this.children.find(n=>n.tag==='button'&&n.type==='submit')||null:null;}
 getAttribute(name){return this.attributes[name]??(name==='action'?this.action:null);}
 setAttribute(name,value){this.attributes[name]=String(value);}
 focus(){}
}
class Option{constructor(label,value){this.textContent=label;this.value=String(value);this.dataset={};}}
function fixture(crsfActive,formAction='/param3save'){
 const elements={},calls=[],timers=[];
 const body=new Element('body'),form=new Element('form'),rootNode=new Element('div'),save=new Element('button');
 form.action=formAction;rootNode.id='remote-controls';save.type='submit';body.append(form);form.append(rootNode,save);
 const document={readyState:'complete',getElementById:id=>elements[id]??=(new Element()),createElement:tag=>new Element(tag)};
 elements['remote-controls']=rootNode;
 for(const id of ['access','message','maintenance','status','discover','selectModel','assign','backup','model','preview','review','apply','kind','file','clone','previewText','safety','devices','fields','field','modelId','confirm','cancel'])elements[id]=new Element(id==='file'?'input':'div');
 elements.modelId.value='0';
 async function fetch(url,options={}){
  const path=url.split('?')[0],values=options.body instanceof URLSearchParams?Object.fromEntries(options.body):{};
  calls.push({path,method:options.method||'GET',...values});let data={},ok=true;
  if(path==='/remote/session')data={nonce:'a'.repeat(32)};
  else if(path==='/remote/devices')data={crsfActive,devices:crsfActive?[{address:238,name:'TX',fieldCount:1,generation:1}]:[],...(crsfActive?{rfKnown:true,connected:false,armedKnown:true,armed:false,ch5:172}: {})};
  else if(path==='/remote/upload')data={fileHash:'b'.repeat(64),calibration:'Keep this Remote\'s calibration'};
  else if(path==='/remote/apply'||path==='/remote/review')data={ok:true};
  else if(path==='/remote/action'){if(values.action==='status'&&!crsfActive){ok=false;data={code:'UNSUPPORTED_BEHAVIOR',message:'CRSF inactive'};}else data={id:1,state:'queued'};}
  return {ok,json:async()=>structuredClone(data),blob:async()=>new Blob(['{}'])};
 }
 vm.runInNewContext(embedded,{document,module:{exports:{}},Option,fetch,URLSearchParams,FormData,confirm:()=>true,URL:{createObjectURL:()=>'',revokeObjectURL:()=>{}},setTimeout:fn=>{timers.push(fn);return timers.length;}});
 return {elements,calls,timers,document,rootNode,body,form,save};
}
const settle=()=>new Promise(setImmediate);
async function settingsSaveAtPageEnd(){
 const f=fixture(true);await settle();
 assert.equal(f.rootNode.parentNode,f.body,'live/file/review panel follows the settings form');
 assert.equal(f.form.id,'elrs-settings');
 assert.equal(f.save.parentNode,f.body,'the native Save button follows the complete CRSF panel');
 assert.equal(f.save.getAttribute('form'),'elrs-settings','external Save remains associated with CRSF settings');
 assert(f.body.children.indexOf(f.save)>f.body.children.indexOf(f.rootNode));
 assert(f.rootNode.innerHTML.indexOf('id="review"')>f.rootNode.innerHTML.indexOf('CRSF settings files'));
 const general=fixture(false,'/paramsave');await settle();
 assert.equal(general.save.parentNode,general.form,'General Settings Save stays with its original form');
 assert.equal(general.save.getAttribute('form'),null);
}
async function nonCrsfFilesAndReview(){
 const f=fixture(false);await settle();
 assert.match(f.elements.access.textContent,/session active/i);
 f.elements.file.files=[{size:2}];f.elements.kind.value='backup';
 await f.elements.preview.onclick();await f.elements.apply.onclick();await f.elements.review.onclick();
 assert(f.calls.some(c=>c.path==='/remote/apply'&&c.method==='POST'));
 assert(f.calls.some(c=>c.path==='/remote/review'&&c.method==='POST'));
 assert(!f.calls.some(c=>c.path==='/remote/action'&&c.action==='status'),'inactive radio UI must not request RF status');
}
portalLayout(html);
assert(embedded.endsWith(script),'gzip JavaScript differs from the current UI source');
assert(embedded.includes('const settingsForm=remoteControls.closest("form")'));
Promise.all([settingsSaveAtPageEnd(),nonCrsfFilesAndReview()]).then(()=>console.log('CRSF page order, deferred asset, footer Save association, legacy-field removal, compressed UI and inactive-radio file/review flow PASS')).catch(e=>{console.error(e);process.exitCode=1;});
