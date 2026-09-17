"""//! 不调用真实服务或扩展，验证旧 Debian 和旧扩展停用迁移。"""
import json
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
from unittest.mock import patch
import session
import package_state

with tempfile.TemporaryDirectory() as temporary:
    base = Path(temporary)
    os.environ.update(HOME=str(base), XDG_CONFIG_HOME=str(base / 'config'), XDG_STATE_HOME=str(base / 'state'),
                      DBUS_SESSION_BUS_ADDRESS='unix:path=/private-test', XDG_CURRENT_DESKTOP='GNOME')
    calls = []
    def manager(*arguments):
        calls.append(arguments)
        if arguments[0] == 'is-enabled':
            return 1, 'disabled', ''
        if arguments[0] == 'is-active':
            return 0, 'active' if arguments[1] == 'graphical-session.target' else 'inactive', ''
        return 0, '', ''
    args = SimpleNamespace(prefix='/usr', login=True, startup=None, enable=False, disable=False,
                           recover=False, restart=False, enable_extension=False)
    with patch.object(session, 'systemctl', manager), patch.object(session, 'preflight', lambda _: []), \
         patch.object(session, 'read_state', lambda: {}), patch.object(session, 'write_state') as write, \
         patch.object(session, 'configure') as configure, patch.object(session, 'command', lambda _: (1, '', '')), \
         patch.object(Path, 'read_text', side_effect=FileNotFoundError):
        assert session.setup(args) == 0
        configure.assert_called_once_with(Path('/usr'), 'session', False)
        assert ('start', session.UNIT) not in calls
        assert not write.call_args.args[0]['enabled']
    # 首次升级只捕获当时已存在账户；之后创建的账户能首次启用服务和扩展。
    old = SimpleNamespace(pw_uid=1000, pw_name='old', pw_dir='/home/old')
    new = SimpleNamespace(pw_uid=1001, pw_name='new', pw_dir='/home/new')
    migration = base / 'package-state.json'
    with patch.object(package_state.pwd, 'getpwall', lambda: [old]):
        package_state.record_upgrade(migration, upgrading=True)
    with patch.object(package_state.pwd, 'getpwall', lambda: [old, new]):
        package_state.record_upgrade(migration, upgrading=True)
    assert json.loads(migration.read_text())['legacy_users'] == [package_state.identity(old)]
    for account, enabled in [(old, False), (new, True)]:
        calls.clear()
        commands = []
        def package_command(arguments):
            commands.append(arguments)
            return 0, session.UUID if arguments == ['gnome-extensions', 'list', '--disabled'] else '', ''
        with patch.object(package_state.pwd, 'getpwuid', lambda _: account), \
             patch.object(session, 'legacy_user', lambda: package_state.legacy_user(migration)), \
             patch.object(session, 'systemctl', manager), patch.object(session, 'preflight', lambda _: []), \
             patch.object(session, 'read_state', lambda: {}), patch.object(session, 'write_state') as write, \
             patch.object(session, 'configure') as configure, patch.object(session, 'handshake', lambda _: {'ready': True}), \
             patch.object(session, 'command', package_command), patch.object(Path, 'is_dir', lambda _: True):
            assert session.setup(args) == 0
            configure.assert_called_once_with(Path('/usr'), 'session', enabled)
            assert (('start', session.UNIT) in calls) == enabled
            assert ['gnome-extensions', 'enable', session.UUID] in commands
            assert write.call_args.args[0]['extension'] == 'registered'
    # 旧 Debian 已启用服务，首次加入扩展时默认 disabled 不是用户停用证据。
    # 同一场景只有明确的 disabled-extensions 才阻止首次启用。
    def enabled_manager(*arguments):
        if arguments[0] == 'is-enabled':
            return 0, 'enabled', ''
        return manager(*arguments)
    for explicitly_disabled in [False, True]:
        commands = []
        def extension_command(arguments):
            commands.append(arguments)
            if arguments == ['gnome-extensions', 'list', '--disabled']:
                return 0, session.UUID, ''
            if arguments == ['gsettings', 'get', 'org.gnome.shell', 'disabled-extensions']:
                return 0, repr([session.UUID] if explicitly_disabled else []), ''
            return 0, '', ''
        with patch.object(session, 'legacy_user', lambda: True), \
             patch.object(session, 'systemctl', enabled_manager), patch.object(session, 'preflight', lambda _: []), \
             patch.object(session, 'read_state', lambda: {}), patch.object(session, 'write_state') as write, \
             patch.object(session, 'configure') as configure, patch.object(session, 'handshake', lambda _: {'ready': True}), \
             patch.object(session, 'command', extension_command), patch.object(Path, 'is_dir', lambda _: True), \
             patch.object(Path, 'read_text', side_effect=FileNotFoundError):
            assert session.setup(args) == 0
            configure.assert_called_once_with(Path('/usr'), 'session', True)
            assert (['gnome-extensions', 'enable', session.UUID] in commands) != explicitly_disabled
            assert write.call_args.args[0]['extension'] == ('preserved-disabled' if explicitly_disabled else 'registered')
    prefix = base / 'prefix'
    (prefix / 'share/gnome-shell/extensions' / session.UUID).mkdir(parents=True)
    (prefix / 'share/qingjian').mkdir(parents=True)
    (prefix / 'share/qingjian/install-options.json').write_text(json.dumps({'previous_install': True, 'previous_extension': True}))
    args.prefix = str(prefix)
    commands = []
    def command(arguments):
        commands.append(arguments)
        return 0, session.UUID if arguments == ['gnome-extensions', 'list', '--disabled'] else '', ''
    with patch.object(session, 'systemctl', manager), patch.object(session, 'preflight', lambda _: []), \
         patch.object(session, 'read_state', lambda: {}), patch.object(session, 'write_state') as write, \
         patch.object(session, 'configure'), patch.object(session, 'command', command):
        assert session.setup(args) == 0
        assert write.call_args.args[0]['extension'] == 'preserved-disabled'
        assert ['gnome-extensions', 'enable', session.UUID] not in commands
print('通过：旧服务停用保留、新账户首次登记、首次加入扩展与明确扩展停用分别处理')
