"use strict";
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const source=fs.readFileSync(__dirname+'/../src/src/CRSF/crsf_wifi.h','utf8');
const start=source.indexOf('static const char crsfCalScript[] =');
const fragment=source.slice(start,source.indexOf('static const char *wmBuildCRSFCAL',start));
const html=[...fragment.matchAll(/"(?:[^"\\]|\\.)*"/g)].map(m=>JSON.parse(m[0])).join('');
const script=html.slice(html.indexOf('>')+1,html.lastIndexOf('</script>'));
const elements={'remote-controls':{dataset:{}}},timers=[];
let requests=0,resolve,reject;
const schedule=(fn,delay)=>{timers.push({fn,delay});};
vm.runInNewContext(script,{
 window:{},document:{getElementById:id=>elements[id]??={textContent:'--',dataset:{}}},
 fetch:()=>{requests++;return new Promise((yes,no)=>{resolve=yes;reject=no;});},
 setInterval:schedule,setTimeout:schedule
});
const settle=()=>new Promise(setImmediate);
(async()=>{
 assert.equal(requests,1);
 for(let i=0;i<5;i++)for(const timer of [...timers])timer.fn();
 assert.equal(requests,1,'Slow ADC reads must not queue more HTTP requests');
 resolve({json:async()=>({ok:true,roll:100,pitch:200,throttle:300,yaw:400})});await settle();
 assert.equal(elements.elrs_roll_live.textContent,100);
 assert.equal(timers.length,1);assert.equal(timers[0].delay,500);
 elements['remote-controls'].dataset.busy='1';timers.shift().fn();assert.equal(requests,1,'radio loading pauses background calibration requests');elements['remote-controls'].dataset.busy='0';timers.shift().fn();assert.equal(requests,2);
 reject(Error('temporary HTTP failure'));await settle();
 assert.equal(elements.elrs_roll_live.textContent,'--');assert.equal(timers.length,1);
 timers.shift().fn();assert.equal(requests,3);
 resolve({json:async()=>({ok:true,roll:101,pitch:201,throttle:301,yaw:401})});await settle();
 assert.equal(elements.elrs_roll_live.textContent,101);assert.equal(timers.length,1);
 console.log('Calibration polling stays at one request and resumes after successful or failed reads PASS');
})().catch(error=>{console.error(error);process.exitCode=1;});
