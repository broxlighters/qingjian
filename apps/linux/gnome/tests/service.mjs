//! 用生产 Hello 验证连接代次独立于 Bind/Hide，错误名只包含协议原因。
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {BUS, reasonCode} from '../extension/protocol.js';

const scope = {BUS, reasonCode, Main: {layoutManager: {monitors: [{x: 0, y: 0, width: 1920, height: 1080}]}},
    GLib: {get_monotonic_time: () => 1000, Variant: class { constructor(type, value) { this.type = type; this.value = value; } }}};
vm.createContext(scope);
vm.runInContext(fs.readFileSync(new URL('../extension/service.js', import.meta.url), 'utf8')
    .replace(/^import .*;$/gm, '').replace('export class PanelService', 'globalThis.PanelService = class PanelService'), scope);
for (const disconnected of [false, true]) {
    const service = Object.create(scope.PanelService.prototype);
    Object.assign(service, {_owner: ':1.5', _ownerGeneration: 0, _serial: 0, _submissions: {epoch: '99'}});
    const pending = [];
    service._call = (method, value, callback) => pending.push(() => callback(method === 'GetId' ? 'session-bus' : 42));
    let response = null, error = null;
    const invocation = {get_sender: () => ':1.5', return_value: value => { response = value; },
        return_dbus_error: (name, message) => { error = [name, message]; }};
    service.HelloAsync([1], invocation);
    ++service._serial; // 同一连接上的 Bind/Hide 不得取消 Hello。
    if (disconnected) ++service._ownerGeneration;
    while (pending.length) pending.shift()();
    if (disconnected) assert.deepEqual(error, [`${BUS}.transport_lost`, 'transport_lost']);
    else {
        assert.equal(error, null);
        assert.equal(JSON.parse(response.value[0]).transport_epoch, '99');
    }
    service._error(invocation, 'arbitrary error message with spaces', 'anchor_unavailable');
    assert.deepEqual(error, [`${BUS}.anchor_unavailable`, 'anchor_unavailable']);
}
const service = Object.create(scope.PanelService.prototype);
let renewed = 0, emitted = null;
Object.assign(service, {_alive: true, _last: {}, _binding: {proof: 'current'},
    _focus: {proofs: {renew: proof => { assert.equal(proof, 'current'); ++renewed; return true; }}},
    _export: {emit_signal: (name, value) => { emitted = [name, value]; }}});
service._emit('Painted', 'token');
assert.equal(renewed, 1);
assert.equal(service._deadline, 501000);
assert.equal(service._last.state, 'Visible');
assert.equal(emitted[0], 'Painted');
console.log('通过：Hello 连接代次与几何请求独立、未知异常不形成非法 D-Bus 错误名');
