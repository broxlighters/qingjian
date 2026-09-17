//! 采集窗口同源按键证据；失焦、工作区、Overview 和锁屏立即撤销租约。
import Clutter from 'gi://Clutter';
import GLib from 'gi://GLib';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {FocusProofs} from './identity.js';

export class FocusTracker {
    constructor(invalidated, changed, key) {
        this.proofs = new FocusProofs(() => GLib.get_monotonic_time(), () => global.display.focus_window);
        this._invalidated = invalidated;
        this._key = key;
        this._signals = [];
        this._windows = new Map();
        this._connect(global.display, 'window-created', (_, window) => this._observe(window));
        this._connect(global.display, 'notify::focus-window', () => this.invalidate());
        this._connect(Main.overview, 'showing', () => this.invalidate());
        this._connect(Main.sessionMode, 'updated', () => this.invalidate());
        this._connect(global.workspace_manager, 'active-workspace-changed', () => this.invalidate());
        this._connect(global.display, 'window-entered-monitor', changed);
        this._connect(global.display, 'workareas-changed', changed);
        this._connect(Main.layoutManager, 'monitors-changed', changed);
        for (const actor of global.get_window_actors()) this._observe(actor.meta_window);
    }

    _connect(object, signal, callback) {
        this._signals.push([object, object.connect(signal, callback)]);
    }

    _observe(window) {
        if (!window || this._windows.has(window)) return;
        const key = window.connect('notify::user-time', () => {
            if (window !== global.display.focus_window) return;
            const event = Clutter.get_current_event();
            if (event?.type() === Clutter.EventType.KEY_PRESS && event.get_time() === window.get_user_time()) {
                if (this.proofs.record(window, event.get_time(), event.get_key_code())) this._key();
            } else if ([Clutter.EventType.BUTTON_PRESS, Clutter.EventType.TOUCH_BEGIN].includes(event?.type())) {
                this.invalidate();
            }
        });
        const unmanaged = window.connect('unmanaged', () => {
            this.invalidate();
            for (const id of this._windows.get(window) ?? []) window.disconnect(id);
            this._windows.delete(window);
        });
        this._windows.set(window, [key, unmanaged]);
    }

    invalidate() {
        this.proofs.invalidate();
        this._invalidated();
    }

    destroy() {
        this.proofs.invalidate();
        for (const [object, id] of this._signals) object.disconnect(id);
        for (const [window, ids] of this._windows) for (const id of ids) window.disconnect(id);
        this._windows.clear();
        this._signals = [];
    }
}
