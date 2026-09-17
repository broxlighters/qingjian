//! 几何变化撤销旧纹理、点击及绘制回调，仍允许同窗口有效凭证重新绑定。
import assert from 'node:assert/strict';
import {globals, FocusProofs} from './extension.mjs';
let now = 0;
let monitor = 0;
let raster = 1;
let area = {x: 0, y: 0, width: 1280, height: 720};
const signals = [];
const connections = new Map();
let sequence = 0;
const window = {get_pid: () => 42, get_client_type: () => 0, get_monitor: () => monitor,
    get_stable_sequence: () => 7, get_buffer_rect: () => ({x: 100, y: 100}),
    connect(name, fn) { connections.set(++sequence, {name, fn}); return sequence; },
    disconnect(id) { connections.delete(id); }};
globals.global.display.focus_window = window;
globals.global.display.get_monitor_scale = () => raster;
globals.Main.layoutManager.getWorkAreaForMonitor = () => ({...area});
globals.GLib.Variant = class { constructor(_signature, args) { this.args = args; } };
const probe = new globals.ProbeExtension();
probe._proofs = new FocusProofs(() => now, () => globals.global.display.focus_window);
probe._owner = ':1.42'; probe._version = 2; probe._serial = 0; probe._busId = 'a'.repeat(32);
probe._windowSignals = [];
probe._actor = {visible: true, width: 100, height: 30, reactive: true,
    hide() { this.visible = false; }, set_content() {}, set_position() {}};
probe._export = {emit_signal(name, value) { signals.push([name, ...value.args]); }};
probe._busCall = (_method, _sender, done) => done(42);
const source = [probe._busId, ':1.17', '/org/freedesktop/portal/inputcontext/1', 'b'.repeat(32), '2', 123, 38, 10, 20, 1, 30, 1];
const invocation = {get_sender: () => ':1.42', return_value() {}, return_dbus_error() { throw Error('unexpected rejection'); }};
probe._proofs.record(window, 123, 38);
probe.BindAsync(['10', ...source], invocation);
const proof = probe._binding.proof;
probe._prepared = '10'; probe._pressed = {token: '10', button: 1};
// 缩放相同的另一输出仍是新几何，且绑定订阅真实main-monitor属性。
assert([...connections.values()].some(item => item.name === 'notify::main-monitor'));
monitor = 1;
probe._position();
probe._position();
assert.deepEqual(signals, [['GeometryChanged', '10']]);
assert.equal(probe._pressed, null);
assert.equal(probe._prepared, null);
assert.equal(probe._binding, null);
assert.equal(probe._actor.visible, false);
assert(probe._proofs.valid(proof));
assert.equal(connections.size, 0);
now = 200000;
probe.BindAsync(['11', ...source], invocation);
assert.equal(probe._binding.proof, proof);
assert.equal(proof.expires, 500000); // Bind及几何重试不延长证明期限。
area = {...area, width: 800};
probe._position();
assert.deepEqual(signals.at(-1), ['GeometryChanged', '11']);
probe.BindAsync(['12', ...source], invocation);
raster = 1.6666666666666667;
probe._position();
assert.deepEqual(signals.at(-1), ['GeometryChanged', '12']);
probe.BindAsync(['13', ...source], invocation);
globals.global.display.focus_window = null;
probe._position();
assert.deepEqual(signals.at(-1), ['Hidden', '13']);
assert(!probe._proofs.valid(proof));
console.log('通过：跨同倍率输出/工作区/倍率的新几何代次、去重、期限与失焦撤销');
