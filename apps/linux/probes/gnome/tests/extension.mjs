//! 运行真实扩展方法的生命周期回归；桌面 API 由有界 mock 提供，不代替 GJS 实测。
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const identitySource = fs.readFileSync(new URL('../extension/identity.js', import.meta.url), 'utf8');
const {FocusProofs} = await import(`data:text/javascript,${encodeURIComponent(identitySource)}`);
const handlers = new Map();
let next = 1;
let painted = 0;
const stage = {
    connect: (_, fn) => { const id = next++; handlers.set(id, fn); return id; },
    disconnect: id => handlers.delete(id),
};
const globals = {
    FocusProofs,
    Extension: class {},
    global: {stage, display: {disconnect() {}}},
    Main: {
        sessionMode: {isLocked: false, hasWindows: true, disconnect() {}},
        overview: {visible: false, disconnect() {}},
        layoutManager: {removeChrome() {}, connect() { return 1; }, disconnect() {},
            getWorkAreaForMonitor: () => ({x: 0, y: 0, width: 1280, height: 720})},
    },
    GLib: {Variant: class {}, source_remove() {}, get_monotonic_time: () => 0},
    Gio: {bus_unwatch_name() {}, bus_unown_name() {}},
};
const source = fs.readFileSync(new URL('../extension/extension.js', import.meta.url), 'utf8')
    .replace(/^import .*;\n/gm, '').replace('export default class ProbeExtension', 'globalThis.ProbeExtension = class ProbeExtension');
vm.createContext(globals);
vm.runInContext(source, globals);
const probe = new globals.ProbeExtension();
probe._proofs = new FocusProofs(() => 0, () => null);
probe._observed = new Map();
probe._owner = ':1.42'; probe._serial = 1; probe._prepared = '9007199254740993';
probe._actor = {visible: false, mapped: true, show() { this.visible = true; }, hide() { this.visible = false; }, set_content() {}, destroy() {}};
probe._export = {emit_signal(name) { if (name === "Painted") painted++; }, unexport() {}};
const invocation = {get_sender: () => ':1.42', return_value() {}, return_dbus_error() { throw Error('unexpected rejection'); }};
probe.ShowAsync([probe._prepared], invocation);
probe.ShowAsync([probe._prepared], invocation);
assert.equal(handlers.size, 1);
for (const fn of [...handlers.values()]) fn();
assert.equal(painted, 1);
assert.equal(handlers.size, 0);
probe.ShowAsync([probe._prepared], invocation);
assert.equal(handlers.size, 0);
probe._hide(); probe._hide();
assert.equal(handlers.size, 0);
probe._prepared = '9007199254740994';
probe.ShowAsync([probe._prepared], invocation);
const late = [...handlers.values()][0];
probe._hide();
late();
assert.equal(painted, 1);
assert.equal(handlers.size, 0);
probe._prepared = '9007199254740995';
probe.ShowAsync([probe._prepared], invocation);
probe.disable();
assert.equal(handlers.size, 0);
assert.equal(probe._actor, null);
const protocol = fs.readFileSync(new URL('../extension/protocol.js', import.meta.url), 'utf8');
const data = await import(`data:text/javascript,${encodeURIComponent(protocol)}`);
assert(data.validBuffer('18446744073709551615', 1600, 900, 6400, 5760000));
for (const id of ['0', '01', '18446744073709551616', '-1', '1.5'])
    assert(!data.validBuffer(id, 1, 1, 4, 4));
assert(!data.validBuffer('1', 1601, 900, 6404, 5763600));
assert(!data.validBuffer('1', 10, 10, 40, 399));
console.log('通过：重复 Show/Hide、晚到绘制、disable 清理与身份/位图边界');
// 真正执行Prepare异步完成：期间monitor倍率变化，拒绝成功应答和prepared复活。
globals.validBuffer = data.validBuffer;
globals.BUS = data.BUS;
let readCompleted;
let accepted = 0;
let rejected = 0;
globals.Gio.Cancellable = class { cancel() {} };
globals.GioUnix = {InputStream: class {
    read_bytes_async(_length, _priority, _cancel, callback) { readCompleted = () => callback(this, {}); }
    read_bytes_finish() { return {get_size: () => 12000}; }
    close_async(_priority, _cancel, callback) { callback(this, {}); }
    close_finish() {}
}};
globals.St = {ImageContent: {new_with_preferred_size: () => ({set_bytes: () => true})}};
globals.Cogl = {PixelFormat: {RGBA_8888_PRE: 1}};
globals.GLib.timeout_add = () => 1;
globals.global.stage.get_context = () => ({get_backend: () => ({get_cogl_context: () => ({})})});
const probe2 = new globals.ProbeExtension();
const window = {get_monitor: () => 0, disconnect() {}};
probe2._proofs = {valid: () => true, revoke() {}};
probe2._owner = ':1.42';
probe2._serial = 10;
probe2._prepared = null;
probe2._binding = {window, identity: '10', raster: 1};
probe2._windowSignals = [];
probe2._actor = {hide() {}, set_content() {}, set_size() {}};
probe2._export = {emit_signal() {}};
globals.global.display.focus_window = window;
globals.global.display.get_monitor_scale = () => 2;
probe2.PrepareAsync(['10', 0, 100, 30, 400, 12000], {
    get_sender: () => ':1.42',
    get_message: () => ({get_unix_fd_list: () => ({get_length: () => 1, get: () => 3})}),
    return_value() { accepted++; },
    return_dbus_error() { rejected++; },
});
readCompleted();
assert.equal(accepted, 0);
assert.equal(rejected, 1);
assert.equal(probe2._binding, null);
assert.equal(probe2._prepared, null);
console.log('通过：Prepare异步期间输出倍率变化作废旧纹理和应答');
// 实际enable注册的按下/释放处理器必须绑定按下时的submission。
const inputHandlers = new Map();
let pointers = 0;
globals.Clutter = {EVENT_STOP: 1, EVENT_PROPAGATE: 0, Actor: class {
    connect(name, handler) { inputHandlers.set(name, handler); }
    transform_stage_point() { return [true, 4, 5]; }
}};
globals.Main.layoutManager.addChrome = () => {};
globals.Gio.DBus = {session: {}};
globals.Gio.DBusExportedObject = {wrapJSObject: () => ({export() {}, emit_signal(name) { if (name === 'Pointer') pointers++; }})};
globals.Gio.bus_own_name_on_connection = () => 1;
globals.Gio.BusNameOwnerFlags = {NONE: 0};
globals.global.get_window_actors = () => [];
globals.global.display.connect = () => 1;
globals.Main.overview.connect = () => 1;
globals.Main.sessionMode.connect = () => 1;
globals.XML = data.XML;
globals.PATH = data.PATH;
const probe3 = new globals.ProbeExtension();
probe3.enable();
probe3._proofs = {valid: () => true, revoke() {}};
probe3._binding = {raster: 1}; probe3._prepared = '101'; probe3._actor.mapped = true;
const button = {get_button: () => 1, get_coords: () => [4, 5]};
inputHandlers.get('button-press-event')(null, button);
probe3._prepared = '102';
inputHandlers.get('button-release-event')(null, button);
assert.equal(pointers, 0);
inputHandlers.get('button-press-event')(null, button);
inputHandlers.get('button-release-event')(null, button);
assert.equal(pointers, 1);
inputHandlers.get('button-release-event')(null, button);
assert.equal(pointers, 1);
console.log('通过：press后换submission与重复release不发送Pointer');
// 实际 Bind 异步 PID 回调重排：同来源新帧继承在途 claim，旧回调不得覆盖。
let now = 0;
const proofWindow = {get_pid: () => 42, get_client_type: () => 0, get_monitor: () => 0,
    get_stable_sequence: () => 7, connect: () => 1, disconnect() {}};
globals.global.display.focus_window = proofWindow;
globals.global.display.get_monitor_scale = () => 1;
globals.Main.layoutManager.getWorkAreaForMonitor = () => ({x: 0, y: 0, width: 1280, height: 720});
const proofTracker = new FocusProofs(() => now, () => globals.global.display.focus_window);
const probe4 = new globals.ProbeExtension();
probe4._proofs = proofTracker; probe4._owner = ':1.42'; probe4._version = 2;
probe4._serial = 0; probe4._busId = 'a'.repeat(32); probe4._windowSignals = [];
probe4._actor = {visible: false, mapped: true, hide() { this.visible = false; },
    show() { this.visible = true; }, set_content() {}};
probe4._export = {emit_signal(name) { if (name === 'Painted') painted++; }};
const callbacks = [];
probe4._busCall = (_method, _sender, callback) => callbacks.push(callback);
const args = ['10', probe4._busId, ':1.17', '/org/freedesktop/portal/inputcontext/1', 'b'.repeat(32), '2', 123, 38, 10, 20, 1, 30, 1];
let bindings = 0, stale = 0;
const bindingInvocation = {get_sender: () => ':1.42', return_value() { bindings++; }, return_dbus_error() { stale++; }};
proofTracker.record(proofWindow, 123, 38);
probe4.BindAsync(args, bindingInvocation);
probe4.WithdrawAsync([], invocation);
probe4.BindAsync(['11', ...args.slice(1)], bindingInvocation);
assert.equal(callbacks.length, 2);
callbacks[0](42); callbacks[1](42);
assert.equal(bindings, 1); assert.equal(stale, 1);
assert.equal(probe4._binding.identity, '11');
assert(proofTracker.valid(probe4._binding.proof));
// Show 到 after-paint 之间凭证过期，不确认已经失效的内容。
probe4._prepared = '11';
probe4.ShowAsync(['11'], invocation);
const beforePaint = painted;
now = 500001;
for (const fn of [...handlers.values()]) fn();
assert.equal(painted, beforePaint);
probe4.HideAsync([], invocation);
probe4._owner = ':1.42';
// v2 撤销后的晚到 Prepare/Show 不能借无绑定色块通路复活。
const rejectedBefore = stale;
probe4.PrepareAsync(['12', 0, 100, 30, 400, 12000], bindingInvocation);
probe4._prepared = '12';
probe4.ShowAsync(['12'], bindingInvocation);
assert.equal(stale, rejectedBefore + 2);
assert.equal(bindings, 1);
console.log('通过：在途 Bind/Withdraw 重排、v2 撤销后迟到 Prepare/Show、绘制前凭证到期');

export {globals, FocusProofs};
