// Run against the actual portal HTML exported by CRSF_SWITCH_PREVIEW in check_crsf_settings.py.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const html = fs.readFileSync(process.argv[2], 'utf8');
const selects = [...html.matchAll(/<select\b([^>]*)>([\s\S]*?)<\/select>/g)].map(([, attrs, body]) => ({
  id: attrs.match(/\bid='([^']+)'/)[1],
  value: body.match(/value='(\d+)' selected/)[1],
  options: [...body.matchAll(/value='(\d+)'/g)].map(match => match[1]),
  switch: attrs.includes('data-elrs-switch'),
  validity: '',
  hasAttribute(name) { return name === 'data-elrs-switch' && this.switch; },
  setCustomValidity(message) { this.validity = message; },
  addEventListener(event, callback) { assert.equal(event, 'change'); this.change = callback; }
}));
const document = {
  querySelectorAll(selector) {
    return selects.filter(select => select.switch || selector.includes('#' + select.id));
  }
};
for (const [, script] of html.matchAll(/<script>([\s\S]*?)<\/script>/g)) vm.runInNewContext(script, { document });
assert.equal(selects.length, 16);
assert.ok(selects.every(select => !select.validity));
const aileron = selects.find(select => select.id === 'crlch');
const stop = selects.find(select => select.id === 'csw0');
const originalStop = stop.value;
function change(select, value) {
  assert.ok(select.options.includes(value));
  select.value = value;
  select.change();
}
change(stop, String(Number(aileron.value) + 1));
assert.ok(aileron.validity && stop.validity, 'Gimbal/switch collisions must invalidate both selectors');
change(aileron, String(Number(originalStop) - 1));
assert.ok(selects.every(select => !select.validity), 'Completing a cross-input swap must clear validation');
change(stop, selects.find(select => select.id === 'csw1').value);
assert.ok(stop.validity && selects.find(select => select.id === 'csw1').validity);
for (const select of selects) change(select, select.switch ? '0' : '16');
assert.ok(selects.every(select => !select.validity), 'Any number of None selections must be allowed');
change(aileron, '0'); // CH1 keeps its original HTTP index.
change(stop, '1');
assert.ok(aileron.validity && stop.validity, 'Assigned channels must still reject collisions beside None');
change(stop, '0');
assert.ok(selects.every(select => !select.validity));
console.log('CRSF portal combined channel collision and swap check passed');
