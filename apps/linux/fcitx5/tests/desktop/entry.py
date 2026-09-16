#!/usr/bin/env python3
# //! 隔离桌面验收的 GTK4 输入框，只记录固定测试输入。
import json
import pathlib
import sys

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import GLib, Gtk

GLib.set_prgname("qingjian-gtk-test")
window = Gtk.Window(title="Qingjian isolated GTK test")
entry = Gtk.Entry()
window.set_child(entry)
window.set_default_size(640, 180)
loop = GLib.MainLoop()
window.connect("close-request", lambda *_: loop.quit())


def changed(*_):
    path = pathlib.Path(sys.argv[1])
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps({"text": entry.get_text(), "scale": window.get_scale_factor()}, ensure_ascii=False))
    temporary.replace(path)


entry.connect("changed", changed)
window.connect("notify::scale-factor", changed)
changed()
window.present()
entry.grab_focus()
GLib.timeout_add_seconds(45, lambda: loop.quit())
loop.run()
