// Execute the actual portal script through availability changes and slow requests.
const fs = require('fs'), vm = require('vm'), assert = require('assert');
const html = fs.readFileSync(process.argv[2], 'utf8');
class Element {
  constructor(value='') { this.value=value; this.handlers={}; this.options=[]; this.dataset={live:'1'}; this._text=''; }
  addEventListener(name, callback) { this.handlers[name]=callback; }
  get selectedOptions() { return this.options.filter(option=>option.value===this.value); }
  get textContent() { return this._text; }
  set textContent(text) { this._text=text; this.options=[]; }
  appendChild(option) { this.options.push(option); }
}
const elements = {cdsrc:new Element('1'),cdsrcvalue:new Element('1'),cdmul:new Element('0.5'),cdoff:new Element('0'),cddec:new Element('1'),cspdu:new Element('0'),cdpreview:new Element(),cdstatus:new Element()};
Object.assign(elements,{crpoles:new Element('4'),crgear:new Element('6.55'),crdiam:new Element('64'),crscale:new Element('10'),rpmscale:new Element(),rpmspeed:new Element()});
elements.cdsrc.options=[{value:'1',textContent:'GPS speed (unavailable)',disabled:true}];
const requests=[], timers=[];
const context = {document:{getElementById:id=>elements[id],createElement:()=>new Element()},
  fetch:(url)=>new Promise(resolve=>requests.push({url,resolve})),setInterval:callback=>timers.push(callback)};
vm.createContext(context);
vm.runInContext(html.match(/<script>([\s\S]*?)<\/script>/)[1],context);
const data=(sources,text='50.0',source=1)=>({sources,preview:{source,text,label:'GPS speed',unit:'km/h',value:text==='---'?null:100,available:text!=='---'}});
const gps={id:1,label:'GPS speed',unit:'km/h'};
const battery={id:3,label:'Vehicle battery voltage',unit:'V'};
const flush=async()=>{for(let i=0;i<8;i++)await Promise.resolve();};
async function reply(index,body,ok=true,status=200) { requests[index].resolve({ok,status,json:()=>Promise.resolve(body)}); await flush(); }
(async()=>{
  assert.strictEqual(requests.length,1);
  assert.strictEqual(elements.rpmspeed.hidden,true);
  timers[0]();timers[0]();assert.strictEqual(requests.length,1); // One request at a time.
  await reply(0,data([gps,battery]));
  assert.strictEqual(elements.cdpreview.textContent,'50.0');
  assert.strictEqual(elements.cdstatus.textContent,'GPS speed: 100 km/h');
  assert.deepStrictEqual(elements.cdsrc.options.map(option=>option.value),['14','0','13','1','3']);
  elements.cdsrc.value='3';elements.cdsrc.handlers.change();
  assert.strictEqual(elements.cdsrcvalue.value,'3');
  elements.cdmul.value='0.001';elements.cdmul.handlers.input();
  assert.strictEqual(requests.length,2);
  await reply(1,data([gps,battery],'12.6')); // Stale preview must not overwrite pending edits.
  assert.strictEqual(elements.cdpreview.textContent,'…');
  timers[0]();assert.strictEqual(requests.length,3);
  const pending=new URL(requests[2].url,'http://remote').searchParams;
  assert.strictEqual(pending.get('cdsrc'),'3');assert.strictEqual(pending.get('cdmul'),'0.001');
  await reply(2,data([gps],'---'));
  assert.strictEqual(elements.cdsrc.value,'3');assert.strictEqual(elements.cdsrcvalue.value,'3');
  assert(elements.cdsrc.selectedOptions[0].disabled);assert.strictEqual(elements.cdpreview.textContent,'---');
  timers[0]();await reply(3,data([gps,battery],'0.01'));
  assert(!elements.cdsrc.selectedOptions[0].disabled);assert.strictEqual(elements.cdpreview.textContent,'0.01');
  timers[0]();await reply(4,null,false,503);
  assert.strictEqual(elements.cdsrcvalue.value,'3');assert(elements.cdsrc.selectedOptions[0].disabled);
  assert.strictEqual(elements.cdpreview.textContent,'---');
  elements.cdsrc.value='14';elements.cdsrc.handlers.change();await reply(5,data([],'',14));
  assert.strictEqual(elements.cdsrcvalue.value,'14');assert.strictEqual(elements.cdpreview.textContent,'Blank');
  assert.strictEqual(elements.cdstatus.textContent,'Normal gimbal-controlled speed/speedo');
  for(const id of ['cdmul','cdoff','cddec'])assert(elements[id].disabled);
  elements.cdsrc.value='0';elements.cdsrc.handlers.change();assert(elements.cdmul.disabled);
  elements.cdsrc.value='13';elements.cdsrc.handlers.change();assert(elements.cdmul.disabled);
  elements.cdsrc.value='1';elements.cdsrc.handlers.change();assert(!elements.cdmul.disabled);
  assert.strictEqual(elements.cdmul.value,'0.001');
  assert(elements.rpmscale.hidden);
  elements.cdsrc.value='16';elements.cdsrc.handlers.change();assert(!elements.rpmscale.hidden);
  assert.strictEqual(elements.rpmspeed.hidden,false);
  elements.crgear.value='13.1';elements.crgear.handlers.input();
  elements.cdsrc.value='15';elements.cdsrc.handlers.change();assert(elements.rpmscale.hidden);
  assert.strictEqual(elements.rpmspeed.hidden,false);
  assert.strictEqual(elements.crgear.value,'13.1');
  elements.cdsrc.value='1';elements.cdsrc.handlers.change();assert.strictEqual(elements.rpmspeed.hidden,true);
  assert.strictEqual(elements.crgear.value,'13.1');
  console.log('CRSF portal availability, pending selection, preview races, and single-request polling checks passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
