//! 只在已证明的窗口上转换客户端相对坐标；目标输出控制栅格密度。
import * as Main from 'resource:///org/gnome/shell/ui/main.js';

export function geometry(window, source) {
    if (!window || window.get_client_type() !== 0 || !Number.isFinite(source.scale) ||
        source.scale < 0.5 || source.scale > 4 || !Array.isArray(source.rect) || source.rect.length !== 4 ||
        !source.rect.every(Number.isFinite) || source.rect[2] < 0 || source.rect[3] <= 0)
        throw new Error('anchor_unavailable');
    const monitor = window.get_monitor();
    const raster = global.display.get_monitor_scale(monitor);
    const area = Main.layoutManager.getWorkAreaForMonitor(monitor);
    if (!Number.isFinite(raster) || raster < 0.5 || raster > 4 || area.width <= 0 || area.height <= 0)
        throw new Error('scale_unresolved');
    return {window, monitor, raster, area, source, width: Math.min(1600, Math.floor(area.width * raster)),
        height: Math.min(900, Math.floor(area.height * raster)), raster_source: 'gnome-monitor',
        coordinates: 'client-relative', context_scale: source.scale};
}

export function sameGeometry(first, second) {
    return first.monitor === second.monitor && first.raster === second.raster &&
        ['x', 'y', 'width', 'height'].every(key => first.area[key] === second.area[key]);
}

export function position(binding, width, height) {
    const origin = binding.window.get_buffer_rect();
    const [x, y, , cursorHeight] = binding.source.rect.map(value => value / binding.source.scale);
    const {area} = binding;
    let top = origin.y + y + cursorHeight;
    if (top + height > area.y + area.height) top = origin.y + y - height;
    return [Math.max(area.x, Math.min(origin.x + x, area.x + area.width - width)),
        Math.max(area.y, Math.min(top, area.y + area.height - height))];
}
