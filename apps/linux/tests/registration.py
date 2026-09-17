"""//! 首次登记重试期间仍保留用户新作出的停用选择；显式启用可覆盖。"""
import os
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
from unittest.mock import patch
import session

with tempfile.TemporaryDirectory() as temporary:
    base = Path(temporary)
    os.environ.update(HOME=str(base), XDG_CONFIG_HOME=str(base / 'config'), XDG_STATE_HOME=str(base / 'state'),
                      DBUS_SESSION_BUS_ADDRESS='unix:path=/private-test', XDG_CURRENT_DESKTOP='GNOME')
    prefix = base / 'prefix'
    (prefix / 'share/gnome-shell/extensions' / session.UUID).mkdir(parents=True)
    (prefix / 'share/qingjian').mkdir(parents=True)
    (prefix / 'share/qingjian/install-options.json').write_text(json.dumps({'previous_extension': True}))
    args = SimpleNamespace(prefix=str(prefix), login=True, startup=None, enable=False, disable=False,
                           recover=False, restart=False, enable_extension=False)
    def manager(*arguments):
        if arguments[0] == 'is-enabled':
            return 0, 'enabled', ''
        if arguments[0] == 'is-active':
            return 0, 'active' if arguments[1] == 'graphical-session.target' else 'inactive', ''
        return 0, '', ''
    for initial_failure in ['undiscovered', 'enable-failed']:
        for disabled, force in [(False, False), (True, False), (True, True)]:
            saved = {}
            calls = []
            attempt = 0
            def command(arguments):
                calls.append(arguments)
                if arguments == ['gsettings', 'get', 'org.gnome.shell', 'disabled-extensions']:
                    return 0, repr([session.UUID] if attempt and disabled else []), ''
                if arguments == ['gnome-extensions', 'list', '--disabled']:
                    return 0, session.UUID if attempt else '', ''
                if not attempt and arguments == ['gnome-extensions', 'info', session.UUID] and initial_failure == 'undiscovered':
                    return 1, '', 'not discovered'
                if not attempt and arguments == ['gnome-extensions', 'enable', session.UUID] and initial_failure == 'enable-failed':
                    return 1, '', 'enable failed'
                return 0, '', ''
            def save(value):
                saved.clear()
                saved.update(value)
            args.enable_extension = False
            with patch.object(session, 'systemctl', manager), patch.object(session, 'preflight', lambda _: []), \
                 patch.object(session, 'configure'), patch.object(session, 'read_state', lambda: dict(saved)), \
                 patch.object(session, 'write_state', save), patch.object(session, 'handshake', lambda _: {'ready': True}), \
                 patch.object(session, 'command', command):
                assert session.setup(args) == 0
                assert saved['initialized'] and saved['extension'] == 'pending'
                calls.clear()
                attempt = 1
                args.enable_extension = force
                assert session.setup(args) == 0
                expected_enable = not disabled or force
                assert (['gnome-extensions', 'enable', session.UUID] in calls) == expected_enable
                assert saved['extension'] == ('registered' if expected_enable else 'preserved-disabled')
                if disabled and not force:
                    # 保存保留状态后，下一次登录也不重启；显式恢复仍可执行。
                    calls.clear()
                    assert session.setup(args) == 0
                    assert ['gnome-extensions', 'enable', session.UUID] not in calls
                    args.enable_extension = True
                    assert session.setup(args) == 0 and saved['extension'] == 'registered'
    # 已完成/保留停用状态的显式启用失败也必须保留 pending 重试意图。
    for prior in ['registered', 'preserved-disabled']:
        for disabled_after in [False, True]:
            saved = {'initialized': True, 'extension': prior}
            calls = []
            attempt = 0
            def retry_command(arguments):
                calls.append(arguments)
                if arguments == ['gsettings', 'get', 'org.gnome.shell', 'disabled-extensions']:
                    return 0, repr([session.UUID] if disabled_after else []), ''
                if arguments == ['gnome-extensions', 'list', '--disabled']:
                    return 0, session.UUID, ''
                if arguments == ['gnome-extensions', 'info', session.UUID]:
                    return (1 if not attempt else 0), '', ''
                return 0, '', ''
            args.enable_extension = True
            with patch.object(session, 'systemctl', manager), patch.object(session, 'preflight', lambda _: []), \
                 patch.object(session, 'configure'), patch.object(session, 'read_state', lambda: dict(saved)), \
                 patch.object(session, 'write_state', save), patch.object(session, 'handshake', lambda _: {'ready': True}), \
                 patch.object(session, 'command', retry_command):
                assert session.setup(args) == 0 and saved['extension'] == 'pending'
                args.enable_extension = False
                attempt = 1
                calls.clear()
                assert session.setup(args) == 0
                assert (['gnome-extensions', 'enable', session.UUID] in calls) != disabled_after
                assert saved['extension'] == ('preserved-disabled' if disabled_after else 'registered')
print('通过：未发现/启用失败后重试检查新停用选择；未停用可重试、显式启用可恢复')
