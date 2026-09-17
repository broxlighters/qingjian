//! 非聚焦 Actor、像素上传和指针映射；纹理准备与显示分离，更新时保留旧画面。
import Clutter from 'gi://Clutter';
import Cogl from 'gi://Cogl';
import Gio from 'gi://Gio';
import GioUnix from 'gi://GioUnix';
import GLib from 'gi://GLib';
import St from 'gi://St';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {position} from './geometry.js';

export class Surface {
    constructor(emit, valid) {
        this._emit = emit;
        this._valid = valid;
        this._serial = 0;
        this._regions = [];
        this._interactive = false;
        this._actor = new Clutter.Actor({reactive: false, visible: false});
        this._buttonPress = (_, event) => {
            this._pressed = this._clickable() && event.get_button() === 1 ? {token: this._visible.token, button: 1} : null;
            return this._pressed ? Clutter.EVENT_STOP : Clutter.EVENT_PROPAGATE;
        };
        this._buttonRelease = (_, event) => {
            const pressed = this._pressed;
            this._pressed = null;
            if (!this._clickable()) return Clutter.EVENT_PROPAGATE;
            if (pressed?.token === this._visible.token && pressed.button === event.get_button()) {
                const [stageX, stageY] = event.get_coords();
                const [ok, x, y] = this._actor.transform_stage_point(stageX, stageY);
                if (ok && x >= 0 && y >= 0 && x < this._actor.width && y < this._actor.height) {
                    this._emit('Pointer', this._visible.token, [Math.floor(x * this._visible.raster),
                        Math.floor(y * this._visible.raster), event.get_button()]);
                }
            }
            return Clutter.EVENT_STOP;
        };
        this._scroll = (_, event) => {
            if (!this._clickable()) return Clutter.EVENT_PROPAGATE;
            const direction = event.get_scroll_direction();
            let button = direction === Clutter.ScrollDirection.UP ? 4 : direction === Clutter.ScrollDirection.DOWN ? 5 : 0;
            if (direction === Clutter.ScrollDirection.SMOOTH) {
                const [, dy] = event.get_scroll_delta();
                button = dy < 0 ? 4 : dy > 0 ? 5 : 0;
            }
            if (button && (button === 4 ? this._visible.interactions.previous : this._visible.interactions.next)) {
                this._emit('Pointer', this._visible.token, [0, 0, button]);
                return Clutter.EVENT_STOP;
            }
            return Clutter.EVENT_PROPAGATE;
        };
        Main.layoutManager.addChrome(this._actor);
    }

    _clickable() {
        return this._visible && this._interactive && this._actor.mapped && this._valid(this._visible.token);
    }

    invalidatePointer() {
        this._pressed = null;
        this._interactive = false;
        for (const region of this._regions) region.reactive = false;
    }

    prepare(token, descriptor, width, height, stride, length, logicalWidth, logicalHeight, raster, interactions, done) {
        this._cancel?.cancel();
        this._pending = null;
        const serial = ++this._serial;
        const stream = new GioUnix.InputStream({fd: descriptor, close_fd: true});
        const cancel = new Gio.Cancellable();
        this._cancel = cancel;
        stream.read_bytes_async(length + 1, GLib.PRIORITY_DEFAULT, cancel, (source, result) => {
            let error = null;
            try {
                const bytes = source.read_bytes_finish(result);
                if (serial !== this._serial || !this._valid(token) || bytes.get_size() !== length)
                    throw new Error('buffer_invalid');
                const content = St.ImageContent.new_with_preferred_size(width, height);
                const context = global.stage.get_context().get_backend().get_cogl_context();
                if (!content.set_bytes(context, bytes, Cogl.PixelFormat.RGBA_8888_PRE, width, height, stride))
                    throw new Error('renderer_unavailable');
                this._pending = {token, content, width: logicalWidth, height: logicalHeight, raster, interactions};
            } catch (reason) {
                error = reason.message;
            } finally {
                source.close_async(GLib.PRIORITY_DEFAULT, null, (s, r) => {
                    try { s.close_finish(r); } catch (_) { /* 已关闭。 */ }
                });
                this._emit('Released', token);
            }
            done(error);
        });
    }

    show(token, binding) {
        if (!this._pending || this._pending.token !== token || !this._valid(token)) return false;
        if (this._paint) global.stage.disconnect(this._paint);
        this._visible = this._pending;
        this._pending = null;
        this._actor.set_content(this._visible.content);
        this._actor.set_size(this._visible.width, this._visible.height);
        this.position(binding);
        this.invalidatePointer();
        for (const region of this._regions) region.destroy();
        this._regions = this._visible.interactions.regions.map(([x, y, width, height]) => {
            const raster = this._visible.raster;
            const region = new Clutter.Actor({x: x / raster, y: y / raster,
                width: width / raster, height: height / raster, reactive: false});
            region.connect('button-press-event', this._buttonPress);
            region.connect('button-release-event', this._buttonRelease);
            region.connect('scroll-event', this._scroll);
            this._actor.add_child(region);
            return region;
        });
        this._paint = global.stage.connect('after-paint', () => {
            const signal = this._paint;
            this._paint = 0;
            global.stage.disconnect(signal);
            if (this._actor.mapped && this._actor.visible && this._visible?.token === token && this._valid(token)) {
                this._interactive = true;
                for (const region of this._regions) region.reactive = true;
                this._emit('Painted', token);
            }
        });
        this._actor.show();
        this._actor.queue_redraw();
        return true;
    }

    position(binding) {
        const [x, y] = position(binding, this._actor.width, this._actor.height);
        this._actor.set_position(x, y);
    }

    hide() {
        ++this._serial;
        this._cancel?.cancel();
        this._cancel = null;
        if (this._paint) global.stage.disconnect(this._paint);
        this._paint = 0;
        this._pending = null;
        this._visible = null;
        this.invalidatePointer();
        for (const region of this._regions) region.destroy();
        this._regions = [];
        this._actor.hide();
        this._actor.set_content(null);
    }

    destroy() {
        this.hide();
        Main.layoutManager.removeChrome(this._actor);
        this._actor.destroy();
    }
}
