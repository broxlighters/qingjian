# //! 只修改夹具私有总线中的临时输出；portal 使用系统真实实现且显式启动。
import json
import os
import time
from gi.repository import Gio, GLib


def configure_display(bus, base, scale, dual=False):
    def call(method, params=None):
        return bus.call_sync('org.gnome.Mutter.DisplayConfig', '/org/gnome/Mutter/DisplayConfig',
            'org.gnome.Mutter.DisplayConfig', method, params, None, Gio.DBusCallFlags.NONE, 3000, None).unpack()
    current = call('GetCurrentState')
    monitors = sorted(current[1], key=lambda item: -item[1][0][1]) if dual else current[1][:1]
    configs = []
    left = 0
    for index, (spec, modes, _) in enumerate(monitors):
        mode = modes[0]
        requested = (5 / 3 if index == 0 else 1) if dual else scale
        selected = min(mode[5], key=lambda value: abs(value - requested))
        assert abs(selected - requested) < .01, (requested, mode[5])
        configs.append((left, 0, selected, 0, index == 0, [(spec[0], mode[0], {})]))
        left += round(mode[1] / selected)
    assert len(configs) == (2 if dual else 1)
    call('ApplyMonitorsConfig', GLib.Variant('(uua(iiduba(ssa{sv}))a{sv})',
        (current[0], 1, configs, {'layout-mode': GLib.Variant('u', 1)})))
    time.sleep(.5)
    result = call('GetCurrentState')
    (base / 'scale-state.json').write_text(json.dumps(result, indent=2))
    print('SCALES', [config[2] for config in configs])


def start_settings(bus, base, env, start, text_scale):
    settings = Gio.Settings.new('org.gnome.desktop.interface')
    assert settings.set_double('text-scaling-factor', text_scale)
    Gio.Settings.sync()
    definition = base / 'portal-definitions'
    definition.mkdir()
    (definition / 'gnome.portal').write_text('[portal]\nDBusName=org.freedesktop.impl.portal.desktop.gnome\n'
        'Interfaces=org.freedesktop.impl.portal.Settings;\nUseIn=gnome\n')
    env['XDG_DESKTOP_PORTAL_DIR'] = str(definition)
    for binary in ('xdg-desktop-portal-gnome', 'xdg-desktop-portal'):
        start(['/usr/libexec/' + binary], binary)
        time.sleep(.5)
    for _ in range(50):
        try:
            result = bus.call_sync('org.freedesktop.portal.Desktop', '/org/freedesktop/portal/desktop',
                'org.freedesktop.portal.Settings', 'Read', GLib.Variant('(ss)',
                ('org.gnome.desktop.interface', 'text-scaling-factor')), None, Gio.DBusCallFlags.NONE, 500, None).unpack()[0]
            assert result == text_scale, result
            (base / 'text-scale.json').write_text(json.dumps({'requested': text_scale, 'portal': result}))
            return
        except GLib.Error:
            time.sleep(.1)
    raise RuntimeError('真实 portal settings 未就绪')
