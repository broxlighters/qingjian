//! 有界展示服务；同一连接提前握手，准备、绘制、隐藏与租约各有独立语义。
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {BUS, PATH, XML, Submissions, identity, validBuffer, reasonCode} from './protocol.js';
import {FocusTracker} from './focus.js';
import {geometry, sameGeometry} from './geometry.js';
import {Surface} from './surface.js';

export class PanelService {
    constructor() {
        this._epoch = BigInt(GLib.get_monotonic_time());
        this._geometry = 0n;
        this._alive = true;
        this._serial = 0;
        this._ownerGeneration = 0;
        this._windowSignals = [];
        this._last = {state: 'Idle', backend: 'gnome', reason: null, painted: false};
        this._surface = new Surface((name, token, rest) => this._emit(name, token, rest), token => this._valid(token));
        this._focus = new FocusTracker(() => this._hide('focus_mismatch'), () => {
            this._position();
            // 输出快照不改变连接身份，也不重新协商 Hello；几何重绘可独立进行。
            this._export?.emit_signal('OutputsChanged', new GLib.Variant('(s)', [JSON.stringify(this._monitors())]));
        },
            () => this._surface.invalidatePointer());
        this._export = Gio.DBusExportedObject.wrapJSObject(XML, this);
        this._export.export(Gio.DBus.session, PATH);
        this._name = Gio.bus_own_name_on_connection(Gio.DBus.session, BUS, Gio.BusNameOwnerFlags.NONE, null, null);
        this._expiry = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 50, () => {
            this._focus.proofs.prune();
            if (this._deadline && GLib.get_monotonic_time() >= this._deadline) this._hide('paint_timeout');
            return GLib.SOURCE_CONTINUE;
        });
    }

    _emit(name, token, rest = []) {
        if (!this._alive || !this._export) return;
        if (name === 'Painted') {
            // 实际绘制才续展仍有效的窗口凭证，跨屏重绘不继承快到期的旧租约。
            if (!this._focus.proofs.renew(this._binding?.proof)) { this._hide('focus_mismatch'); return; }
            this._deadline = GLib.get_monotonic_time() + 500000;
            this._last = {...this._last, state: 'Visible', painted: true};
        }
        const signature = name === 'Pointer' ? '(suuu)' : name === 'Lost' ? '(ss)' : '(s)';
        this._export.emit_signal(name, new GLib.Variant(signature, [token, ...rest]));
    }

    _error(invocation, reason, fallback = 'protocol_mismatch') {
        const code = reasonCode(reason, fallback);
        invocation.return_dbus_error(`${BUS}.${code}`, code);
    }

    _monitors() {
        return Main.layoutManager.monitors.map(monitor => [monitor.x, monitor.y, monitor.width, monitor.height]);
    }

    _authorized(invocation) {
        if (this._alive && this._owner && invocation.get_sender() === this._owner) return true;
        this._error(invocation, 'transport_lost');
        return false;
    }

    _call(method, value, callback) {
        Gio.DBus.session.call('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus', method,
            value === null ? null : new GLib.Variant('(s)', [value]), null, Gio.DBusCallFlags.NONE, 250, null,
            (connection, result) => {
                let value = null;
                try { value = connection.call_finish(result).deep_unpack()[0]; } catch (_) { /* 对端退出。 */ }
                if (this._alive) callback(value);
            });
    }

    HelloAsync([version], invocation) {
        const owner = invocation.get_sender();
        if (version !== 1 || (this._owner && this._owner !== owner)) {
            this._error(invocation, 'protocol_mismatch');
            return;
        }
        const generation = this._ownerGeneration;
        this._call('GetId', null, bus => this._call('GetConnectionUnixProcessID', owner, pid =>
            this._call('GetConnectionUnixProcessID', 'org.fcitx.Fcitx5', fcitxPid => {
                if (!bus || !pid || pid !== fcitxPid || generation !== this._ownerGeneration ||
                    (this._owner && this._owner !== owner)) {
                    this._error(invocation, 'transport_lost');
                    return;
                }
                if (!this._owner) {
                    this._owner = owner;
                    this._bus = bus;
                    this._submissions = new Submissions(String(++this._epoch));
                    this._watch = Gio.bus_watch_name_on_connection(Gio.DBus.session, owner,
                        Gio.BusNameWatcherFlags.NONE, null, () => {
                            ++this._ownerGeneration;
                            this._hide('transport_lost');
                            this._owner = null;
                            this._focus.proofs.invalidate();
                            if (this._watch) Gio.bus_unwatch_name(this._watch);
                            this._watch = 0;
                        });
                }
                invocation.return_value(new GLib.Variant('(s)', [JSON.stringify({version: 1, shell: 50,
                    transport_epoch: this._submissions.epoch, format: 'RGBA_8888_PRE', width: 1600,
                    height: 900, length: 5760000, coordinates: ['client-relative'], interaction: true,
                    monitors: this._monitors()})]));
            })));
    }

    BindContextAsync([token, encodedSource], invocation) {
        if (!this._authorized(invocation)) return;
        let source;
        try { source = JSON.parse(encodedSource); } catch (_) { this._error(invocation, 'anchor_unavailable'); return; }
        const data = identity(token);
        if (encodedSource.length > 2048 || !data || source.bus !== this._bus || source.frontend !== 'dbus' ||
            source.coordinates !== 'client-relative' || source.context !== data.context || source.epoch !== data.focus_epoch ||
            Main.overview.visible || Main.sessionMode.isLocked || !Main.sessionMode.hasWindows ||
            !this._submissions.accept(token)) {
            this._error(invocation, 'focus_mismatch');
            return;
        }
        const owner = this._owner;
        const claim = this._focus.proofs.reserve(source, owner);
        if (!claim) { this._error(invocation, 'focus_mismatch'); return; }
        const serial = ++this._serial;
        this._surface.invalidatePointer();
        this._deadline = GLib.get_monotonic_time() + 250000;
        this._call('GetConnectionUnixProcessID', source.sender, pid => {
            if (serial !== this._serial || owner !== this._owner || !this._submissions.valid(token)) {
                this._error(invocation, 'focus_mismatch');
                return;
            }
            const proof = this._focus.proofs.commit(claim, pid);
            if (!proof) { this._error(invocation, 'focus_mismatch'); return; }
            try {
                const binding = geometry(proof.window, source);
                for (const [window, id] of this._windowSignals) window.disconnect(id);
                this._windowSignals = [];
                for (const event of ['position-changed', 'size-changed', 'notify::main-monitor'])
                    this._windowSignals.push([binding.window, binding.window.connect(event, () => this._position())]);
                binding.proof = proof;
                binding.geometry_revision = String(++this._geometry);
                this._binding = binding;
                data.geometry_revision = binding.geometry_revision;
                const next = JSON.stringify(data);
                this._submissions.accept(next);
                this._last = {state: 'Preparing', backend: 'gnome', reason: null, painted: false,
                    frontend: source.frontend, coordinates: binding.coordinates, context_scale: source.scale,
                    raster_scale: binding.raster, raster_source: binding.raster_source, geometry_revision: binding.geometry_revision};
                invocation.return_value(new GLib.Variant('(s)', [JSON.stringify({identity: next,
                    raster: binding.raster, width: binding.width, height: binding.height,
                    raster_source: binding.raster_source, geometry_revision: binding.geometry_revision})]));
            } catch (error) {
                this._error(invocation, error.message, 'anchor_unavailable');
            }
        });
    }

    _valid(token) {
        return this._alive && this._submissions?.valid(token) && this._binding &&
            this._focus.proofs.valid(this._binding.proof) && GLib.get_monotonic_time() < this._deadline &&
            !Main.overview.visible && !Main.sessionMode.isLocked && Main.sessionMode.hasWindows;
    }

    PrepareFrameAsync([token, index, width, height, stride, length, logicalWidth, logicalHeight, encodedInteractions], invocation) {
        if (!this._authorized(invocation)) return;
        if (!this._valid(token) || !validBuffer(width, height, stride, length, logicalWidth, logicalHeight, this._binding.raster)) {
            this._error(invocation, 'buffer_invalid');
            return;
        }
        const list = invocation.get_message().get_unix_fd_list();
        if (!list || list.get_length() !== 1 || index !== 0) {
            this._error(invocation, 'buffer_invalid');
            return;
        }
        let interactions;
        try {
            if (encodedInteractions.length > 16384) throw new Error();
            interactions = JSON.parse(encodedInteractions);
            if (!Array.isArray(interactions.regions) || interactions.regions.length > 130 ||
                !interactions.regions.every(region => Array.isArray(region) && region.length === 5 &&
                    region.every(Number.isFinite) && region[0] >= 0 && region[1] >= 0 && region[2] > 0 && region[3] > 0 &&
                    region[0] + region[2] <= width + 1 && region[1] + region[3] <= height + 1 &&
                    Number.isInteger(region[4]) && region[4] >= -2 && region[4] < 128)) throw new Error();
        } catch (_) { this._error(invocation, 'buffer_invalid'); return; }
        this._surface.prepare(token, list.get(index), width, height, stride, length, logicalWidth, logicalHeight,
            this._binding.raster, interactions, error => {
                if (error || !this._valid(token)) this._error(invocation, error || 'focus_mismatch', 'renderer_unavailable');
                else {
                    this._last.pixel_size = [width, height];
                    this._last.logical_size = [logicalWidth, logicalHeight];
                    this._last.ui_scale = interactions.ui_scale;
                    this._last.text_scale = interactions.text_scale;
                    this._last.system_text_scale = interactions.system_text_scale;
                    this._last.ui_source = interactions.ui_source;
                    this._last.text_source = interactions.text_source;
                    this._last.system_text_source = interactions.system_text_source;
                    this._emit('Prepared', token);
                    invocation.return_value(null);
                }
            });
    }

    ShowAsync([token], invocation) {
        if (!this._authorized(invocation)) return;
        if (!this._valid(token) || !this._surface.show(token, this._binding)) {
            this._error(invocation, 'focus_mismatch');
            return;
        }
        this._last.state = 'AwaitingPaint';
        this._deadline = GLib.get_monotonic_time() + 500000;
        invocation.return_value(null);
    }

    RenewAsync([token], invocation) {
        if (!this._authorized(invocation)) return;
        if (!this._valid(token) || !this._focus.proofs.renew(this._binding.proof)) {
            this._error(invocation, 'focus_mismatch');
            return;
        }
        this._deadline = GLib.get_monotonic_time() + 500000;
        invocation.return_value(null);
    }

    HideAsync([token], invocation) {
        if (!this._authorized(invocation)) return;
        if (this._submissions.hide(token) && !this._submissions.current) this._hide(null, token);
        invocation.return_value(null);
    }

    StatusAsync(_, invocation) {
        invocation.return_value(new GLib.Variant('(s)', [JSON.stringify({...this._last, protocol: 1,
            owner: Boolean(this._owner), transport_epoch: String(this._epoch)})]));
    }

    _position() {
        const binding = this._binding;
        if (!binding || !this._submissions?.current) return;
        const token = this._submissions.current;
        try {
            const next = geometry(binding.window, binding.source);
            if (!sameGeometry(binding, next)) {
                this._surface.hide();
                this._last = {...this._last, state: 'Preparing', painted: false};
                this._emit('GeometryChanged', token);
                this._deadline = GLib.get_monotonic_time() + 250000;
            } else this._surface.position(binding);
        } catch (_) { this._hide('scale_unresolved'); }
    }

    _hide(reason, explicitToken = null) {
        const token = explicitToken ?? this._submissions?.current;
        ++this._serial;
        if (token) this._submissions.hide(token);
        this._surface.hide();
        for (const [window, id] of this._windowSignals) window.disconnect(id);
        this._windowSignals = [];
        this._binding = null;
        this._deadline = 0;
        this._last = {...this._last, state: 'Idle', reason: reason ?? this._last.reason, painted: false};
        if (token) {
            if (reason) this._emit('Lost', token, [reason]);
            this._emit('Hidden', token);
        }
    }

    destroy() {
        ++this._ownerGeneration;
        this._hide('transport_lost');
        this._alive = false;
        this._focus.destroy();
        GLib.source_remove(this._expiry);
        if (this._watch) Gio.bus_unwatch_name(this._watch);
        Gio.bus_unown_name(this._name);
        this._export.unexport();
        this._export = null;
        this._surface.destroy();
    }
}
