//! 窗口证据的行为回归：同 PID 多窗口、IC/epoch 替换、异步重排及重放。
import assert from 'node:assert/strict';
import fs from 'node:fs';
const code = fs.readFileSync(new URL('../extension/identity.js', import.meta.url), 'utf8');
const {FocusProofs} = await import(`data:text/javascript,${encodeURIComponent(code)}`);
let now = 0;
const a = {get_pid: () => 42};
const b = {get_pid: () => 42};
let focused = a;
const tracker = new FocusProofs(() => now, () => focused);
const source = {bus: 'a'.repeat(32), sender: ':1.17', path: '/org/freedesktop/portal/inputcontext/1',
    context: 'b'.repeat(32), epoch: '18446744073709551615', time: 0xfffffff0, code: 38};
const other = {...source, context: 'c'.repeat(32)};
const owner = ':1.20';
assert(tracker.record(a, source.time, source.code));
const claim = tracker.reserve(source, owner);
assert(claim);
// cursor-only 新帧取消旧 Bind 后，同来源合并同一个在途 claim；其他 IC 无权消费。
assert.equal(tracker.reserve(source, owner), claim);
assert.equal(tracker.reserve(other, owner), null);
assert.equal(tracker.reserve(source, ':1.21'), null);
const proof = tracker.commit(claim, 42);
assert(tracker.valid(proof));
assert.equal(tracker.commit(claim, 42), null);
assert.equal(tracker.reserve(other, owner), null);
assert.equal(tracker.reserve({...source, epoch: '2'}, owner), null);
assert.equal(tracker.reserve(source, owner).proof, proof);
assert.equal(tracker.commit({proof}, 43), null);
tracker.revoke();
assert.equal(tracker.reserve(source, owner), null);
assert.equal(tracker.reserve(source, ':1.21'), null);
// A→B→A 后不能凭同 PID 或相同窗口对象接受旧证据。
source.time = 100;
tracker.record(a, source.time, source.code);
const late = tracker.reserve(source, owner);
focused = b; tracker.invalidate();
focused = a; tracker.invalidate();
assert.equal(tracker.commit(late, 42), null);
assert.equal(tracker.reserve(source, owner), null);
// 两个窗口的同一 time/code 歧义，即使已回 A 也拒绝。
focused = b; tracker.record(b, 100, 38);
assert.equal(tracker.reserve(source, owner), null);
// 时间戳从 uint32 高位绕回低位，期限仍用单调时钟。
now = 600000;
source.time = 1;
tracker.record(b, 1, 38);
const wrapped = tracker.reserve(source, owner);
const wrappedProof = tracker.commit(wrapped, 42);
assert(tracker.valid(wrappedProof));
now += 400000;
assert(tracker.renew(wrappedProof));
now += 400000;
tracker.prune();
assert(tracker.valid(wrappedProof));
assert.equal(tracker._history.length, 0);
assert(!Object.hasOwn(wrappedProof, 'time') && !Object.hasOwn(wrappedProof, 'code'));
now += 500001;
assert(!tracker.valid(wrappedProof));
// 迟到 PID 回复、错误 PID、同时新增事件、未知sender/bus 均不能签发。
source.time = 2;
tracker.record(b, 2, 38);
const expired = tracker.reserve(source, owner);
now += 500001;
assert.equal(tracker.commit(expired, 42), null);
source.time = 3;
tracker.record(b, 3, 38);
assert.equal(tracker.reserve({...source, bus: 'bad'}, owner), null);
assert.equal(tracker.reserve({...source, sender: 'org.client.Test'}, owner), null);
const wrong = tracker.reserve(source, owner);
assert.equal(tracker.commit(wrong, 43), null);
tracker.record(b, 4, 39);
assert.equal(tracker.commit(wrong, 42), null);
// 真正 Hide 必须撤销在途 claim；未使用过的新键不受开始渲染前的 Hide 影响。
source.time = 5;
tracker.record(b, 5, 38);
tracker.revoke();
const pending = tracker.reserve(source, owner);
assert(pending);
tracker.revoke();
assert.equal(tracker.reserve(source, owner), null);
assert.equal(tracker.commit(pending, 42), null);
console.log('通过：同 PID 多窗口、同窗口 IC/代次切换、在途 claim 合并、单次签发、重放、uint32 wrap、TTL/lease');
