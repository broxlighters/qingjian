//! 独立实验扩展：有期限的异步 FD 读取、隐藏纹理准备和实际绘制回执。
import Clutter from 'gi://Clutter';
import Cogl from 'gi://Cogl';
import Gio from 'gi://Gio';
import GioUnix from 'gi://GioUnix';
import GLib from 'gi://GLib';
import St from 'gi://St';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {BUS, PATH, XML, validBuffer} from './protocol.js';
import {FocusProofs} from './identity.js';

export default class ProbeExtension extends Extension {
    enable() {
        this._owner = null;
        this._serial = 0;
        this._proofs = new FocusProofs(() => GLib.get_monotonic_time(), () => global.display.focus_window);
        this._observed = new Map();
        this._created = global.display.connect('window-created', (_, window) => this._observe(window));
        for (const actor of global.get_window_actors()) this._observe(actor.meta_window);
        this._expiry = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 100, () => {
            this._proofs.prune();
            if (this._binding && !this._proofs.valid(this._binding.proof)) this._hide();
            return GLib.SOURCE_CONTINUE;
        });
        this._windowSignals = [];
        this._actor = new Clutter.Actor({reactive: false, visible: false});
        this._actor.connect('button-press-event', (_, event) => {
            this._pressed = this._prepared && this._binding && this._proofs.valid(this._binding.proof) && this._actor.mapped ?
                {token: this._prepared, button: event.get_button()} : null;
            return this._pressed ? Clutter.EVENT_STOP : Clutter.EVENT_PROPAGATE;
        });
        this._actor.connect('button-release-event', (_, event) => {
            const pressed = this._pressed;
            this._pressed = null;
            if (!this._prepared || !this._binding || !this._proofs.valid(this._binding.proof) || !this._actor.mapped) return Clutter.EVENT_PROPAGATE;
            if (!pressed || pressed.token !== this._prepared || pressed.button !== event.get_button())
                return Clutter.EVENT_STOP;
            const [stageX, stageY] = event.get_coords();
            const [ok, x, y] = this._actor.transform_stage_point(stageX, stageY);
            if (ok && x >= 0 && y >= 0)
                this._export.emit_signal('Pointer', new GLib.Variant('(suuu)', [
                    this._prepared, Math.floor(x * this._binding.raster),
                    Math.floor(y * this._binding.raster), event.get_button()]));
            return Clutter.EVENT_STOP;
        });
        this._actor.connect('scroll-event', (_, event) => {
            if (!this._prepared || !this._binding || !this._proofs.valid(this._binding.proof) || !this._actor.mapped) return Clutter.EVENT_PROPAGATE;
            const direction = event.get_scroll_direction();
            let button = direction === Clutter.ScrollDirection.UP ? 4 :
                direction === Clutter.ScrollDirection.DOWN ? 5 : 0;
            if (direction === Clutter.ScrollDirection.SMOOTH) {
                const [, dy] = event.get_scroll_delta();
                button = dy < 0 ? 4 : dy > 0 ? 5 : 0;
            }
            if (button) this._export.emit_signal('Pointer', new GLib.Variant('(suuu)', [this._prepared, 0, 0, button]));
            return Clutter.EVENT_STOP;
        });
        Main.layoutManager.addChrome(this._actor);
        this._export = Gio.DBusExportedObject.wrapJSObject(XML, this);
        this._export.export(Gio.DBus.session, PATH);
        this._name = Gio.bus_own_name_on_connection(Gio.DBus.session, BUS,
            Gio.BusNameOwnerFlags.NONE, null, null);
        this._focus = global.display.connect('notify::focus-window', () => this._invalidate());
        this._overview = Main.overview.connect('showing', () => this._invalidate());
        this._session = Main.sessionMode.connect('updated', () => this._invalidate());
        this._monitorEntered = global.display.connect('window-entered-monitor', (_display, _monitor, window) => {
            if (window === this._binding?.window) this._position();
        });
        this._workareas = global.display.connect('workareas-changed', () => this._position());
        this._monitors = Main.layoutManager.connect('monitors-changed', () => this._position());
    }

    _observe(window) {
        if (!window || this._observed.has(window)) return;
        const time = window.connect('notify::user-time', () => {
            if (window !== global.display.focus_window) return;
            const event = Clutter.get_current_event();
            if (event?.type() === Clutter.EventType.KEY_PRESS && event.get_time() === window.get_user_time()) {
                if (this._proofs.record(window, event.get_time(), event.get_key_code())) this._hide(false, true, false);
            } else if ([Clutter.EventType.BUTTON_PRESS, Clutter.EventType.TOUCH_BEGIN].includes(event?.type())) {
                this._invalidate();
            }
            // Mutter 可在同一按键派发中把 user-time 校正为0再次通知；无匹配事件的
            // 属性变化不签发新凭证，也不撤销先前已证明的键。焦点有独立事件负责。
        });
        const unmanaged = window.connect('unmanaged', () => {
            if (this._binding?.window === window) this._invalidate();
            const ids = this._observed.get(window);
            this._observed.delete(window);
            for (const id of ids ?? []) window.disconnect(id);
        });
        this._observed.set(window, [time, unmanaged]);
    }

    _invalidate() {
        this._proofs.invalidate();
        this._hide();
    }

    _busCall(method, value, done) {
        Gio.DBus.session.call('org.freedesktop.DBus', '/org/freedesktop/DBus',
            'org.freedesktop.DBus', method, value === null ? null : new GLib.Variant('(s)', [value]),
            null, Gio.DBusCallFlags.NONE, 250, null, (connection, result) => {
                try { done(connection.call_finish(result).deep_unpack()[0]); }
                catch (_) { done(null); }
            });
    }

    BindAsync([identity, bus, sender, path, context, epoch, time, code, x, y, width, height, clientScale], invocation) {
        if (!this._authorized(invocation)) return;
        const window = global.display.focus_window;
        if (this._version !== 2 || bus !== this._busId || !window || window.get_client_type() !== 0 ||
            !Number.isFinite(clientScale) || clientScale < 0.5 || clientScale > 4 ||
            width < 0 || height <= 0 || Main.overview.visible || Main.sessionMode.isLocked) {
            this._error(invocation, 'AnchorUnavailable');
            return;
        }
        const claim = this._proofs.reserve({bus, sender, path, context, epoch, time, code}, this._owner);
        if (!claim) { this._error(invocation, 'FocusUnproven'); return; }
        this._hide(false, true, false);
        const serial = this._serial;
        const owner = this._owner;
        this._busCall('GetConnectionUnixProcessID', sender, pid => {
            if (serial !== this._serial || this._owner !== owner) {
                this._error(invocation, 'StaleSubmission'); return;
            }
            const proof = this._proofs.commit(claim, pid);
            if (!proof) { this._error(invocation, 'FocusUnproven'); return; }
            const monitor = window.get_monitor();
            const raster = global.display.get_monitor_scale(monitor);
            const area = Main.layoutManager.getWorkAreaForMonitor(monitor);
            this._binding = {identity, window, proof, monitor, raster, area, x: x / clientScale,
                y: y / clientScale, height: height / clientScale};
            for (const event of ['position-changed', 'size-changed', 'notify::main-monitor'])
                this._windowSignals.push([window, window.connect(event, () => this._position())]);
            console.log(`青简原型绑定 frontend=dbus identity=window-key-proof window=${window.get_stable_sequence()} raster=${raster} client=${clientScale}`);
            invocation.return_value(new GLib.Variant('(duu)', [raster,
                Math.min(1600, Math.floor(area.width * raster)), Math.min(900, Math.floor(area.height * raster))]));
        });
    }

    _position() {
        const binding = this._binding;
        if (!binding) return;
        const {window, raster, x, y, height} = binding;
        if (!this._proofs.valid(binding.proof) || window !== global.display.focus_window) {
            this._hide();
            return;
        }
        const monitor = window.get_monitor();
        const area = Main.layoutManager.getWorkAreaForMonitor(monitor);
        if (monitor !== binding.monitor || global.display.get_monitor_scale(monitor) !== raster ||
            ['x', 'y', 'width', 'height'].some(key => area[key] !== binding.area[key])) {
            // 几何改变不撤销同窗焦点证明。先移除纹理/点击与在途回调，再请求新代次。
            const identity = binding.identity;
            this._hide(false, true, false);
            this._export.emit_signal('GeometryChanged', new GLib.Variant('(s)', [identity]));
            return;
        }
        const origin = window.get_buffer_rect();
        let left = origin.x + x;
        let top = origin.y + y + height;
        if (top + this._actor.height > area.y + area.height)
            top = origin.y + y - this._actor.height;
        left = Math.max(area.x, Math.min(left, area.x + area.width - this._actor.width));
        top = Math.max(area.y, Math.min(top, area.y + area.height - this._actor.height));
        this._actor.set_position(left, top);
    }

    _error(invocation, code) {
        invocation.return_dbus_error(`${BUS}.${code}`, code);
    }

    _authorized(invocation) {
        if (this._owner && invocation.get_sender() === this._owner)
            return true;
        this._error(invocation, 'Unauthorized');
        return false;
    }

    HelloAsync([version], invocation) {
        const owner = invocation.get_sender();
        if (![1, 2].includes(version) || (this._owner && owner !== this._owner)) {
            this._error(invocation, 'HandshakeRejected'); return;
        }
        const serial = this._serial;
        const accept = () => {
            if (serial !== this._serial || (this._owner && owner !== this._owner) || !this._export) {
                this._error(invocation, 'HandshakeRejected'); return;
            }
            if (!this._owner) {
                this._owner = owner;
                this._watch = Gio.bus_watch_name_on_connection(Gio.DBus.session,
                    owner, Gio.BusNameWatcherFlags.NONE, null, () => {
                        this._hide();
                        this._owner = null;
                        if (this._watch) Gio.bus_unwatch_name(this._watch);
                        this._watch = 0;
                    });
            }
            this._version = version;
            invocation.return_value(new GLib.Variant('(u)', [version]));
        };
        // v1 只保留无窗口/无点击的独立色块传输探针；候选 Bind 仅接受 v2。
        if (version === 1) { accept(); return; }
        this._busCall('GetId', null, id => {
            if (!id) { this._error(invocation, 'HandshakeRejected'); return; }
            this._busId = id;
            this._busCall('GetConnectionUnixProcessID', owner, bridgePid => {
                this._busCall('GetConnectionUnixProcessID', 'org.fcitx.Fcitx5', fcitxPid => {
                    if (!bridgePid || bridgePid !== fcitxPid) this._error(invocation, 'HandshakeRejected');
                    else accept();
                });
            });
        });
    }

    PrepareAsync([identity, index, width, height, stride, length], invocation) {
        if (!this._authorized(invocation)) return;
        if (!validBuffer(identity, width, height, stride, length)) {
            this._error(invocation, 'BufferInvalid');
            return;
        }
        const binding = this._binding;
        if ((this._version === 2 && (!binding || !this._proofs.valid(binding.proof))) ||
            (binding && binding.identity !== identity)) {
            this._error(invocation, 'StaleSubmission');
            return;
        }
        this._hide(true, true);
        const serial = this._serial;
        const list = invocation.get_message().get_unix_fd_list();
        if (!list || list.get_length() !== 1 || index !== 0) {
            this._error(invocation, 'BufferInvalid');
            return;
        }
        const stream = new GioUnix.InputStream({fd: list.get(index), close_fd: true});
        this._cancel = new Gio.Cancellable();
        const cancel = this._cancel;
        this._timer = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 500, () => {
            this._timer = 0;
            this._hide();
            return GLib.SOURCE_REMOVE;
        });
        stream.read_bytes_async(length + 1, GLib.PRIORITY_DEFAULT, cancel, (source, result) => {
            try {
                const bytes = source.read_bytes_finish(result);
                if (serial !== this._serial || bytes.get_size() !== length)
                    throw new Error('BufferInvalid');
                const content = St.ImageContent.new_with_preferred_size(width, height);
                const context = global.stage.get_context().get_backend().get_cogl_context();
                if (!content.set_bytes(context, bytes, Cogl.PixelFormat.RGBA_8888_PRE,
                    width, height, stride)) throw new Error('TextureUnavailable');
                this._actor.set_content(content);
                const raster = binding?.raster ?? 1;
                this._actor.set_size(width / raster, height / raster);
                this._actor.reactive = Boolean(binding);
                if (binding) this._position();
                else this._actor.set_position(40, 80);
                if (serial !== this._serial) throw new Error('StaleSubmission');
                this._prepared = identity;
                invocation.return_value(new GLib.Variant('(u)', [length]));
            } catch (_) {
                this._error(invocation, 'BufferInvalid');
            } finally {
                // 关闭 memfd 不等待对端，不保留 FD 到显示结束。
                source.close_async(GLib.PRIORITY_DEFAULT, null, (s, r) => {
                    try { s.close_finish(r); } catch (_) { /* 已关闭。 */ }
                });
            }
        });
    }

    ShowAsync([identity], invocation) {
        if (!this._authorized(invocation)) return;
        if (!this._prepared || identity !== this._prepared || (this._version === 2 && !this._binding) ||
            (this._binding && !this._proofs.valid(this._binding.proof)) || Main.sessionMode.isLocked ||
            Main.overview.visible || !Main.sessionMode.hasWindows) {
            this._error(invocation, 'StaleSubmission');
            return;
        }
        // 重复 Show 同一 submission 幂等，不能覆盖旧 signal id。
        if (this._actor.visible) {
            invocation.return_value(null);
            return;
        }
        const serial = this._serial;
        const paint = global.stage.connect('after-paint', () => {
            global.stage.disconnect(paint);
            if (this._paint !== paint) return;
            this._paint = 0;
            if (serial === this._serial && this._actor?.mapped && this._actor.visible &&
                identity === this._prepared && !Main.overview.visible && !Main.sessionMode.isLocked &&
                (this._version !== 2 || (this._binding && this._proofs.valid(this._binding.proof))))
                this._export.emit_signal('Painted', new GLib.Variant('(s)', [identity]));
        });
        this._paint = paint;
        this._actor.show();
        invocation.return_value(null);
    }

    WithdrawAsync(_, invocation) {
        if (!this._authorized(invocation)) return;
        this._hide(false, true, false);
        invocation.return_value(null);
    }

    HideAsync(_, invocation) {
        if (!this._authorized(invocation)) return;
        this._hide();
        if (this._watch) Gio.bus_unwatch_name(this._watch);
        this._watch = 0;
        this._owner = null;
        invocation.return_value(null);
    }

    RenewAsync([identity], invocation) {
        if (!this._authorized(invocation)) return;
        if (!this._prepared || identity !== this._prepared || !this._actor.visible ||
            (this._binding && !this._proofs.renew(this._binding.proof))) {
            this._error(invocation, 'StaleSubmission');
            return;
        }
        if (this._timer) GLib.source_remove(this._timer);
        this._timer = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 500, () => {
            this._timer = 0;
            this._hide();
            return GLib.SOURCE_REMOVE;
        });
        invocation.return_value(null);
    }

    _hide(keepBinding = false, keepProof = false, notify = true) {
        if (!keepProof) this._proofs?.revoke();
        this._pressed = null;
        const token = this._prepared ?? this._binding?.identity;
        if (!keepBinding) {
            for (const [window, id] of this._windowSignals ?? []) window.disconnect(id);
            this._windowSignals = [];
            this._binding = null;
            if (notify && token && this._export)
                this._export.emit_signal('Hidden', new GLib.Variant('(s)', [token]));
        }
        ++this._serial;
        this._prepared = null;
        this._cancel?.cancel();
        this._cancel = null;
        if (this._timer) GLib.source_remove(this._timer);
        this._timer = 0;
        if (this._paint) global.stage.disconnect(this._paint);
        this._paint = 0;
        this._actor?.hide();
        this._actor?.set_content(null);
    }

    disable() {
        this._hide();
        global.display.disconnect(this._focus);
        global.display.disconnect(this._created);
        global.display.disconnect(this._monitorEntered);
        global.display.disconnect(this._workareas);
        Main.layoutManager.disconnect(this._monitors);
        if (this._expiry) GLib.source_remove(this._expiry);
        this._expiry = 0;
        for (const [window, ids] of this._observed) for (const id of ids) window.disconnect(id);
        this._observed.clear();
        this._proofs.invalidate();
        Main.overview.disconnect(this._overview);
        Main.sessionMode.disconnect(this._session);
        if (this._watch) Gio.bus_unwatch_name(this._watch);
        this._watch = 0;
        this._owner = null;
        Gio.bus_unown_name(this._name);
        this._export.unexport();
        this._export = null;
        Main.layoutManager.removeChrome(this._actor);
        this._actor.destroy();
        this._actor = null;
    }
}
