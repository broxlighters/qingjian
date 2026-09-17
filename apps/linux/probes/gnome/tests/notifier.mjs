//! 运行扩展真实窗口通知适配层，覆盖Mutter时间校正。
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const identity = fs.readFileSync(new URL('../extension/identity.js', import.meta.url), 'utf8');
const {FocusProofs} = await import(`data:text/javascript,${encodeURIComponent(identity)}`);
const source = fs.readFileSync(new URL('../extension/extension.js', import.meta.url), 'utf8')
    .replace(/^import .*;\n/gm, '').replace('export default class ProbeExtension', 'globalThis.ProbeExtension = class ProbeExtension');
const globals = {Extension: class {}, global: {display: {}}};
vm.createContext(globals);vm.runInContext(source, globals);
// user-time 是属性通知，Mutter 时间校正可在同一键派发中再发 user-time=0。
const windowHandlers = new Map();
let userTime = 200;
let eventType = 1;
globals.Clutter = {EventType: {KEY_PRESS: 1, BUTTON_PRESS: 4, TOUCH_BEGIN: 13}};
globals.Clutter.get_current_event = () => ({type: () => eventType, get_time: () => 200, get_key_code: () => 38});
const correctedWindow = {get_pid: () => 42, get_user_time: () => userTime,
    connect(name, callback) { windowHandlers.set(name, callback); return name; }, disconnect() {}};
globals.global.display.focus_window = correctedWindow;
const corrected = new globals.ProbeExtension();
corrected._proofs = new FocusProofs(() => 0, () => globals.global.display.focus_window);
corrected._observed = new Map(); corrected._windowSignals = []; corrected._serial = 0;
corrected._observe(correctedWindow);
windowHandlers.get('notify::user-time')();
const recorded = corrected._proofs._current;
assert(recorded);
userTime = 0;
windowHandlers.get('notify::user-time')();
assert.equal(corrected._proofs._current, recorded);
eventType = 4;
windowHandlers.get('notify::user-time')();
assert.equal(corrected._proofs._current, null);
console.log('通过：同一按键后 user-time=0 校正不清新证据，真实指针按下撤销');

globals.Clutter.get_current_event = () => null;
windowHandlers.get('notify::user-time')();
assert.equal(corrected._proofs._current, null);
globals.Clutter.get_current_event = () => ({type: () => 1, get_time: () => 200, get_key_code: () => 38});
userTime = 200;windowHandlers.get('notify::user-time')();
assert(corrected._proofs._current);
globals.Clutter.get_current_event = () => ({type: () => 13});
windowHandlers.get('notify::user-time')();
assert.equal(corrected._proofs._current, null);
