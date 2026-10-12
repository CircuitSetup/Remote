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
const ids={};for(const [,tag,id] of html.matchAll(/<([a-z]+)[^>]*\bid='([^']+)'/g))ids[id]=new Element(tag);
const document={readyState:'complete',getElementById:id=>ids[id],createElement:tag=>new Element(tag),querySelectorAll:()=>[]};
for(const [,script]of html.matchAll(/<script>([\s\S]*?)<\/script>/g))vm.runInNewContext(script,{document});
const field=name=>ids.lmfields.children.find(x=>x._text.trim()===name)?.children[0];
const hidden=name=>ids.lmvalues.children.find(x=>x.name===name)?.value;
const action=(row,name)=>row.children.at(-1).children.find(x=>x.textContent===name).click();
assert.equal(ids.lml.children.length,16);assert.equal(ids.lmvalues.children.length,178);
assert.equal(hidden('clfn0'),'2');assert.equal(hidden('clx0'),'-90');
assert.equal(hidden('clfn15'),'3');assert.equal(hidden('cmsrc15'),'33');assert.equal(hidden('cmch15'),'16');
action(ids.lmm.children.at(-1),'Delete');assert.equal(hidden('cmch15'),'0');
action(ids.lml.children[0],'Edit');assert.ok(ids.lme.open);
field('Func').value=3;field('Func').onchange();
assert.ok(field('V1').children.every(x=>(Number(x.value)&127)<=16),'L01 cannot reference itself or future rules');
field('V1').value=133;field('V2').value=6;field('AND switch').value=0;
ids.lmapply.click();assert.equal(hidden('cla0'),'133');assert.equal(hidden('clx0'),'0');
action(ids.lml.children[1],'Edit');field('Func').value=4;field('Func').onchange();
assert.ok(field('V1').children.some(x=>x.value==='145'),'L02 can use !L01');
field('V1').value=17;field('V2').value=5;ids.lmapply.click();
assert.equal(hidden('cla1'),'17');assert.equal(hidden('clfn1'),'4');
action(ids.lml.children[0],'Clear');assert.equal(hidden('clfn0'),'0');assert.equal(hidden('cla0'),'0');
action(ids.lml.children[8],'Edit');field('Func').value=3;field('Func').onchange();
assert.ok(field('V1').children.some(x=>x.value==='24'),'L09 can use L08');
assert.ok(field('V1').children.every(x=>![25,153,26,154].includes(Number(x.value))),'MAX and self-reference are excluded');
field('V1').value=24;field('V2').value=133;ids.lmapply.click();assert.equal(hidden('cla8'),'24');
action(ids.lml.children[15],'Edit');field('Func').value=4;field('Func').onchange();
assert.ok(field('V1').children.some(x=>x.value==='160'),'L16 can use !L15');
field('V1').value=26;field('V2').value=32;field('AND switch').value=154;ids.lmapply.click();
assert.ok(ids.lml.children[15].textContent.startsWith('L16'),'L16 is labeled correctly');
ids.lmadd.click();
assert.equal(field('Source').children.find(x=>x.value==='25').textContent,'MAX','MAX source ID remains unchanged');
assert.equal(field('Source').children.find(x=>x.value==='33').textContent,'L16');
assert.ok(field('Switch').children.some(x=>x.value==='161'),'All logical switch inversions are available');
assert.ok(field('Switch').children.every(x=>![25,153].includes(Number(x.value))),'MAX cannot be a gate');
ids.lmapply.click();assert.ok(ids.lme.open,'New mix requires a selected Source');
field('Channel').value=9;field('Source').value=25;field('Weight (%)').value=-50;field('Offset (%)').value=10;field('Switch').value=145;field('Multiplex').value=2;
ids.lmapply.click();assert.equal(hidden('cmwt1'),'-50');assert.equal(hidden('cmsw1'),'145');
assert.equal(hidden('cmch1'),'9');
action(ids.lmm.children[1],'Up');assert.equal(hidden('cmsrc0'),'25');assert.equal(hidden('cmsrc1'),'17');

ids.lmadd.click();field('Weight (%)').value='';ids.lmcancel.click();
assert.equal(ids.lmfields.children.length,0,'Cancel removes invalid draft controls from the outer form');
ids.lmadd.click();ids.lmapply.click();ids.lmcancel.click();
assert.equal(ids.lmfields.children.length,0,'Cancel removes custom validity after failed Apply');
ids.lmadd.click();
assert.equal(field('Channel').value,'9','Add uses the selected destination group');
field('Source').value=25;
let prevented=false;
ids.lme.keydown({key:'Enter',target:ids.lmcancel,preventDefault:()=>prevented=true});
if(!prevented)ids.lmcancel.click();
assert.ok(!prevented,'Enter on Cancel keeps native button activation');
assert.equal(ids.lmcount.textContent,'2/16 additional lines','Keyboard Cancel does not commit a mix');
ids.lmadd.click();field('Weight (%)').value='';ids.lme.close();
assert.equal(ids.lmfields.children.length,0,'Escape/close also removes invalid draft controls');

ids.csw0=new Element('select');ids.csw0.value=16;
action(ids.lml.children[7],'Clear');
action(ids.lmm.children.find(r=>r.textContent.includes('Base Stop')),'Base');
ids.lmadd.click();assert.equal(field('Channel').value,'16','Add uses the selected base destination');ids.lmcancel.click();
action(ids.lmm.children[0],'Down');
assert.equal(hidden('cmsrc0'),'17','Reordering uses the row channel even after selecting another group');
ids.lmadd.click();assert.equal(field('Channel').value,'9','Reordering selects its destination group');ids.lmcancel.click();
action(ids.lmm.children[1],'Up');assert.equal(hidden('cmsrc0'),'25');
action(ids.lmm.children[0],'Delete');assert.equal(hidden('cmch0'),'0');assert.equal(hidden('cmwt0'),'0');
for(let i=0;i<15;i++){ids.lmadd.click();field('Source').value=25;ids.lmapply.click();}
assert.equal(ids.lmcount.textContent,'16/16 additional lines');assert.ok(ids.lmadd.disabled);
ids.lmadd.click();assert.ok(!ids.lme.open,'Seventeenth mix cannot open an editor');
action(ids.lmm.children.filter(r=>!r.textContent.includes('Base')).at(-1),'Edit');
field('Source').value=33;ids.lmapply.click();assert.equal(hidden('cmsrc15'),'33');
action(ids.lmm.children.filter(r=>!r.textContent.includes('Base')).at(-1),'Up');
assert.equal(hidden('cmsrc14'),'33','Mix 16 can move past mix 15');
console.log('Actual Logic/Mixes editor: source restrictions, inversion, canonical fields, ordering, deletion and sixteen-line capacity passed');
