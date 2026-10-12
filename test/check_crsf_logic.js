// Execute the actual generated editor; storage/HTTP validation is covered by the C++ harness.
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const html=fs.readFileSync(process.argv[2],'utf8');
class Element{
  constructor(tag){this.tag=tag;this.tagName=tag.toUpperCase();this.children=[];this._text='';this._value='';this.validity='';}
  get textContent(){return this._text+this.children.map(x=>x.textContent).join('');}
  set textContent(x){this._text=String(x);this.children=[];}
  get value(){return this._value;}
  set value(x){this._value=String(x);}
  appendChild(e){this.children.push(e);return e;}
  replaceChildren(){this.children=[];}
  addEventListener(n,f){this[n]=f;}
  setCustomValidity(x){this.validity=x;}
  reportValidity(){return !this.validity&&(!this.required||this.value!=='')&&
    (this.type!=='number'||(Number.isInteger(Number(this.value))&&Number(this.value)>=this.min&&Number(this.value)<=this.max));}
  showModal(){this.open=true;}
  close(){this.open=false;this.onclose?.();}
  click(){if(!this.disabled)this.onclick?.();}
  scrollIntoView(){}
  focus(){}
}

function boot(logic,mixes){
  const page=logic?html.replace(/const L=\[[\s\S]*?\],M=\[[\s\S]*?\];/,()=>`const L=${JSON.stringify(logic)},M=${JSON.stringify(mixes)};`):html;
  const ids={};for(const [,tag,id]of page.matchAll(/<([a-z]+)[^>]*\bid='([^']+)'/g))ids[id]=new Element(tag);
  const document={readyState:'complete',getElementById:id=>ids[id],createElement:tag=>new Element(tag),querySelectorAll:()=>[]};
  for(const [,script]of page.matchAll(/<script>([\s\S]*?)<\/script>/g))vm.runInNewContext(script,{document});
  const field=name=>ids.lmfields.children.find(x=>x._text.trim()===name)?.children[0];
  const hidden=name=>ids.lmvalues.children.find(x=>x.name===name)?.value;
  const action=(row,name)=>row.children.at(-1).children.find(x=>x.textContent===name);
  const logical=name=>ids.lml.children.find(r=>r.children[0].textContent===name);
  const choices=name=>field(name).children.filter(x=>!x.disabled).map(x=>Number(x.value));
  return {ids,field,hidden,action,logical,choices};
}
const offLogic=()=>Array.from({length:16},()=>[0,0,0,0,0]);
const offMixes=()=>Array.from({length:16},()=>[0,0,0,0,0,0]);
{
  const {ids,field,hidden,action,logical,choices}=boot();
  assert.equal(ids.lml.children.length,2,'Only the populated L01 and L16 are displayed');
  assert.equal(ids.lmm.children.length,2);assert.equal(ids.lmvalues.children.length,178);
  assert.equal(ids.llcount.textContent,'2/16 rows');
  assert.equal(hidden('clfn15'),'3');assert.equal(hidden('cmsrc15'),'33');
  action(ids.lmm.children.at(-1),'Edit').click();
  assert.ok(choices('Source').includes(33)&&choices('Source').includes(161));
  assert.ok(!choices('Source').includes(18)&&!choices('Source').includes(146));
  assert.equal(field('Source').children.find(x=>x.value==='25').textContent,'MAX');
  assert.ok(field('Switch').children.find(x=>x.value==='154').disabled,'Saved missing L09 is unavailable');
  ids.lmapply.click();assert.ok(ids.lme.open,'Missing gate cannot silently become unconditional');
  assert.equal(hidden('cmsw15'),'154');field('Switch').value=0;ids.lmapply.click();assert.equal(hidden('cmsw15'),'0');
  const staleMixEdit=action(ids.lmm.children.at(-1),'Edit');
  action(ids.lmm.children.at(-1),'Delete').click();staleMixEdit.click();assert.ok(!ids.lme.open,'Deleted mix cannot be edited');
  const staleLogicEdit=action(logical('L16'),'Edit');
  action(logical('L16'),'Delete').click();staleLogicEdit.click();assert.ok(!ids.lme.open,'Deleted logic cannot be edited');
  action(ids.lmm.children[0],'Delete').click();action(logical('L01'),'Delete').click();
  assert.equal(ids.lml.children.length,0);assert.equal(ids.lmm.children.length,0);
  ids.lmadd.click();assert.ok(choices('Source').every(id=>(id&127)<17||id===25),'Unpopulated logic cannot be selected');
  field('Source').value=17;ids.lmapply.click();assert.ok(ids.lme.open,'Forged empty logic selection cannot apply');ids.lmcancel.click();
  ids.lladd.click();assert.equal(ids.lmtitle.textContent,'L01');ids.lmapply.click();
  assert.ok(ids.lme.open,'Adding requires a populated function');assert.equal(ids.lml.children.length,0);
  field('Func').value=2;field('Func').onchange();field('V1').value=4;field('V2 (%)').value=-90;ids.lmapply.click();
  assert.equal(hidden('clfn0'),'2');assert.equal(hidden('clx0'),'-90');assert.equal(ids.lml.children.length,1);
  ids.lladd.click();assert.equal(ids.lmtitle.textContent,'L02');field('Func').value=3;field('Func').onchange();
  assert.ok(choices('V1').includes(17)&&choices('V1').includes(145));
  assert.ok(!choices('V1').includes(18)&&!choices('V1').includes(146)&&!choices('V1').includes(25));
  field('V1').value=17;field('V2').value=5;ids.lmapply.click();assert.equal(hidden('cla1'),'17');
  ids.lladd.click();ids.lmcancel.click();assert.equal(ids.llcount.textContent,'2/16 rows');
  ids.lladd.click();ids.lme.oncancel();assert.equal(ids.lmfields.children.length,0,'Cancelled draft controls are removed');
}
{
  const logic=offLogic(),mixes=offMixes();
  for(let i=0;i<16;i++)logic[i]=[1,1,0,0,0];
  for(let i=0;i<15;i++)mixes[i]=[9,25,0,100,0,0];
  const {ids,field,hidden,action,logical,choices}=boot(logic,mixes);
  assert.equal(ids.lml.children.length,16);assert.ok(ids.lladd.disabled);
  ids.lladd.click();assert.ok(!ids.lme.open,'Seventeenth logical row cannot be added');
  action(logical('L08'),'Delete').click();assert.equal(hidden('clfn7'),'0');assert.equal(hidden('clfn8'),'1');
  ids.lmadd.click();assert.ok(!choices('Source').includes(24)&&!choices('Source').includes(152));
  assert.ok(choices('Source').includes(26)&&choices('Source').includes(33));ids.lmcancel.click();
  ids.lladd.click();assert.equal(ids.lmtitle.textContent,'L08','Add reuses a gap without renumbering later rows');
  field('Func').value=3;field('Func').onchange();assert.ok(choices('V1').includes(23));
  assert.ok(!choices('V1').includes(24)&&!choices('V1').includes(26));
  field('V1').value=23;field('V2').value=5;ids.lmapply.click();assert.ok(ids.lladd.disabled);
  ids.lmadd.click();field('Channel').value=9;field('Source').value=33;field('Switch').value=161;ids.lmapply.click();
  assert.equal(hidden('cmsrc15'),'33');assert.equal(ids.lmcount.textContent,'16/16 additional lines');assert.ok(ids.lmadd.disabled);
  action(ids.lmm.children.at(-1),'Up').click();assert.equal(hidden('cmsrc14'),'33');
  ids.lmadd.click();assert.ok(!ids.lme.open,'Seventeenth mix cannot be added');
}

for(const [logicalRef,mixRef]of[[[3,17,5,0,0],null],[[3,5,145,0,0],null],[[1,1,0,-90,145],null],[null,[9,145,0,100,0,2]],[null,[9,25,145,100,0,2]]]){
  const logic=offLogic(),mixes=offMixes();logic[0]=[1,1,0,0,0];
  if(logicalRef)logic[1]=logicalRef;if(mixRef)mixes[0]=mixRef;
  const {ids,field,hidden,action,logical}=boot(logic,mixes);
  const del=action(logical('L01'),'Delete');assert.ok(del.disabled,'Sources, V2 and AND/switch gates protect referenced rows');
  del.click();assert.equal(hidden('clfn0'),'1');
  action(logical('L01'),'Edit').click();field('Func').value=0;field('Func').onchange();ids.lmapply.click();
  assert.ok(ids.lme.open,'Function Off cannot bypass referenced-delete protection');ids.lmcancel.click();assert.equal(hidden('clfn0'),'1');
  if(logicalRef)action(logical('L02'),'Delete').click();if(mixRef)action(ids.lmm.children[0],'Delete').click();
  assert.ok(!action(logical('L01'),'Delete').disabled);action(logical('L01'),'Delete').click();assert.equal(hidden('clfn0'),'0');
}
{
  const logic=offLogic(),mixes=offMixes();logic[0]=[1,1,0,-95,0];logic[15]=[1,1,0,0,0];mixes[0]=[9,25,0,-95,33,0];
  const {hidden,action,logical}=boot(logic,mixes);
  assert.ok(!action(logical('L16'),'Delete').disabled,'Threshold, Weight and Offset values do not count as references');
  action(logical('L16'),'Delete').click();assert.equal(hidden('clfn15'),'0');
}
{
  const logic=offLogic(),mixes=offMixes();logic[15]=[1,1,0,0,0];mixes[0]=[9,25,161,100,0,2];
  const {hidden,action,logical}=boot(logic,mixes);assert.ok(action(logical('L16'),'Delete').disabled);
  action(logical('L16'),'Delete').click();assert.equal(hidden('clfn15'),'1','High inverted IDs protect L16');
}
console.log('Populated Logic/Mixes editor: sparse rows, add/edit/delete, stable IDs, populated choices, unavailable saved references, cancellation, order and capacity passed');
