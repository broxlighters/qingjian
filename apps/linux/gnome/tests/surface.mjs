//! 使用生产 Surface 方法验证命中子 Actor、无效操作和后续有效事件。
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

class Actor {
    constructor(properties) { Object.assign(this, properties); this.handlers = {}; this.children = []; }
    connect(name, callback) { this.handlers[name] = callback; }
    add_child(child) { this.children.push(child); }
    set_content(content) { this.content = content; }
    set_size(width, height) { Object.assign(this, {width, height}); }
    set_position() {}
    show() { this.visible = this.mapped = true; }
    hide() { this.visible = this.mapped = false; }
    destroy() { this.destroyed = true; }
    queue_redraw() {}
    transform_stage_point(x, y) { return [true, x, y]; }
}
const stage = {connect: (_, callback) => { stage.paint = callback; return 1; }, disconnect: () => {}};
const scope = {Clutter: {Actor, EVENT_STOP: 1, EVENT_PROPAGATE: 0, ScrollDirection: {UP: 0, DOWN: 1, SMOOTH: 2}},
    global: {stage}, Main: {layoutManager: {addChrome() {}, removeChrome() {}}}, position: () => [0, 0]};
vm.createContext(scope);
const source = fs.readFileSync(new URL('../extension/surface.js', import.meta.url), 'utf8')
    .replace(/^import .*;$/gm, '').replace('export class Surface', 'globalThis.Surface = class Surface');
vm.runInContext(source, scope);
const emitted = [];
const surface = new scope.Surface((...event) => emitted.push(event), () => true);
surface._pending = {token: 'current', width: 100, height: 80, raster: 2,
    interactions: {regions: [[10, 10, 60, 20, 0], [10, 50, 60, 20, 2]], previous: false, next: true}};
assert(surface.show('current', {}));
stage.paint();
assert.equal(surface._actor.reactive, false);
assert.equal(surface._regions.length, 2);
assert(surface._regions.every(region => region.reactive));
const event = (button = 1, coordinates = [10, 10], direction = 0) => ({get_button: () => button,
    get_coords: () => coordinates, get_scroll_direction: () => direction, get_scroll_delta: () => [0, 0]});
const fire = (name, value) => surface._regions[0].handlers[name](null, value);
// 阴影和空槽没有拾取 Actor；模拟由真实子 Actor 冒泡的非左键与页边界事件。
assert.equal(fire('button-press-event', event(3)), 0);
assert.equal(fire('button-release-event', event(3)), 1);
assert.equal(fire('scroll-event', event(1, [10, 10], 0)), 0);
assert(surface._clickable() && surface._regions.every(region => region.reactive));
fire('button-press-event', event());
fire('button-release-event', event());
assert.deepEqual(JSON.parse(JSON.stringify(emitted)), [['Painted', 'current'], ['Pointer', 'current', [20, 20, 1]]]);
assert(surface._clickable());
surface.invalidatePointer();
assert(!surface._clickable() && surface._regions.every(region => !region.reactive));
surface.hide();
assert.equal(surface._regions.length, 0);
console.log('通过：生产 Surface 命中区域、无效操作、页边界与后续有效点击');
