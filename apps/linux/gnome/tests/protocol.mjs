//! 生产协议身份、乱序、Hide 墓碑、64位和位图尺寸边界。
import assert from 'node:assert/strict';
import {Submissions, identity, validBuffer, reasonCode} from '../extension/protocol.js';

const token = (submission, geometry = '0', epoch = '99') => JSON.stringify({generation: '1',
    context: 'a'.repeat(32), revision: '2', transport_epoch: epoch, focus_epoch: '3',
    submission: String(submission), geometry_revision: geometry});
const frames = new Submissions('99');
assert(frames.accept(token('9007199254740993')));
assert(frames.valid(token('9007199254740993')));
assert(frames.accept(token('9007199254740994')));
assert(!frames.accept(token('9007199254740993')));
assert(frames.hide(token('9007199254740994')));
assert(!frames.accept(token('9007199254740994')));
assert(frames.accept(token('18446744073709551615')));
assert(!frames.accept(token('18446744073709551616')));
assert(!frames.accept(token('5', '0', '98')));
assert.equal(identity(token('1'))?.context, 'a'.repeat(32));
assert.equal(identity(token('1').replace('"revision":"2"', '"revision":2')), null);
assert(validBuffer(1600, 900, 6400, 5760000, 800, 450, 2));
assert(!validBuffer(1600, 900, 6400, 5760000, 1600, 900, 2));
assert(!validBuffer(1601, 900, 6404, 5763600, 1601, 900, 1));
assert.equal(reasonCode('focus_mismatch'), 'focus_mismatch');
assert.equal(reasonCode('TypeError: arbitrary text\nwith spaces'), 'protocol_mismatch');
assert.equal(reasonCode('Cannot read property', 'anchor_unavailable'), 'anchor_unavailable');
console.log('通过：生产协议身份、乱序、Hide 墓碑和位图边界');
