//! 五档倍率、屏幕上下翻转和工作区边界的纯几何回归。
import assert from 'node:assert/strict';
import fs from 'node:fs';
let source = fs.readFileSync(new URL('../extension/geometry.js', import.meta.url), 'utf8')
    .replace(/^import .*;\n/gm, '').replaceAll('export function', 'function') +
    '\nexport {geometry, sameGeometry, position};';
const module = await import(`data:text/javascript,${encodeURIComponent('const Main={layoutManager:{getWorkAreaForMonitor:()=>({x:100,y:20,width:1000,height:700})}};'+
    'const global={display:{get_monitor_scale:()=>1.25}};'+source)}`);
const window = {get_client_type: () => 0, get_monitor: () => 0,
    get_buffer_rect: () => ({x: 200, y: 100})};
for (const scale of [0.75, 1, 1.25, 1.5, 2]) {
    const binding = module.geometry(window, {scale, rect: [40, 80, 4, 20]});
    assert.equal(binding.raster, 1.25);
    assert.deepEqual(module.position(binding, 200, 100), [200 + 40 / scale, 100 + 100 / scale]);
}
const bottom = module.geometry(window, {scale: 1, rect: [40, 680, 4, 20]});
assert.deepEqual(module.position(bottom, 200, 100), [240, 620]);
assert.throws(() => module.geometry(window, {scale: 0, rect: [0, 0, 0, 10]}));
assert.throws(() => module.geometry({...window, get_client_type: () => 1}, {scale: 1, rect: [0, 0, 0, 10]}));
console.log('通过：生产几何五档倍率、翻转和边界');
