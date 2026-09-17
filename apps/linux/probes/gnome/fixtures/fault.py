# //! 只针对夹具进程注入故障并读取Actor撤窗；不操作用户Fcitx。
import json
import time
from gi.repository import Gio, GLib


def inject_fault(bus, process, state, until, fault):
    before = state()
    assert before['visible'] and before['mapped'], before
    began = time.monotonic()
    if fault == 'exit':
        process.terminate()
    elif fault == 'pause':
        process.send_signal(19)
    elif fault == 'overview':
        bus.call_sync('org.gnome.Shell', '/org/gnome/Shell', 'org.freedesktop.DBus.Properties',
            'Set', GLib.Variant('(ssv)', ('org.gnome.Shell', 'OverviewActive', GLib.Variant('b', True))),
            None, Gio.DBusCallFlags.NONE, 3000, None)
    until(lambda: not state()['visible'])
    after = state()
    print(json.dumps({'fault': fault, 'before': before, 'after': after,
        'hidden_ms': (time.monotonic() - began) * 1000}, ensure_ascii=False))
    assert not after['mapped'], after
    if fault == 'pause':
        process.send_signal(18)
