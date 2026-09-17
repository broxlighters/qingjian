#!/usr/bin/python3
# //! 私有桌面的 GTK 单框、同窗双框、同 PID 双 mapped 窗口。
import json
import os
import sys
from pathlib import Path
import gi
gi.require_version('Gtk', '4.0')
from gi.repository import GLib, Gtk

GLib.set_prgname('qingjian-gtk-test')
scenario = os.environ.get('QINGJIAN_TEST_SCENARIO', 'single')
entries = [Gtk.Entry() for _ in range(1 if scenario == 'single' else 2)]
windows = []
if scenario == 'fields':
    window = Gtk.Window(title='QJ isolated GTK fields')
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
    for entry in entries:
        box.append(entry)
    window.set_child(box)
    windows.append(window)
else:
    for index, entry in enumerate(entries):
        window = Gtk.Window(title=f'QJ isolated GTK window {index}')
        window.set_child(entry)
        windows.append(window)
loop = GLib.MainLoop()


def record(*_):
    path = Path(sys.argv[1])
    temporary = path.with_suffix('.tmp')
    texts = [entry.get_text() for entry in entries]
    temporary.write_text(json.dumps({'text': texts[0], 'texts': texts,
        'scale': windows[0].get_scale_factor()}, ensure_ascii=False))
    temporary.replace(path)


for entry in entries:
    entry.connect('changed', record)
for window in reversed(windows):
    window.set_default_size(640, 240 if scenario == 'fields' else 180)
    window.connect('close-request', lambda *_: loop.quit())
    window.present()
entries[0].grab_focus()
record()
GLib.timeout_add_seconds(45, lambda: loop.quit())
loop.run()
