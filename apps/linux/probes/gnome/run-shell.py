#!/usr/bin/python3
# //! 隔离 GNOME 会话验证；不连接用户会话、不激活外部 service、不替代实机验收。
import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess as sp
import tempfile
import time
from fixtures.kimpanel import prepare as prepare_kimpanel
from fixtures.instrument import install as instrument, diagnostics

parser = argparse.ArgumentParser()
parser.add_argument('--extension', type=Path)
parser.add_argument('--sender', type=Path)
parser.add_argument('--diagnostic-windows', action='store_true')
parser.add_argument('--diagnostic-actor', action='store_true')
parser.add_argument('--output', type=Path, default=Path('target/gnome-stage0'))
parser.add_argument('--native', choices=['gtk', 'qt', 'firefox', 'editor', 'terminal'])
parser.add_argument('--mouse', action='store_true')
parser.add_argument('--fault', choices=['pause', 'exit', 'overview'])
parser.add_argument('--addon', type=Path)
parser.add_argument('--server', type=Path)
parser.add_argument('--firefox', type=Path)
parser.add_argument('--qt-pythonpath', type=Path)
parser.add_argument('--scenario', choices=['single', 'fields', 'windows'], default='single')
parser.add_argument('--scale', choices=[100, 125, 150, 167, 200], type=int, default=100)
parser.add_argument('--text-scale', type=float)
parser.add_argument('--ui-scale', type=int, default=100)
parser.add_argument('--no-follow-text', action='store_true')
parser.add_argument('--kimpanel', type=Path)
parser.add_argument('--fallback', action='store_true')
parser.add_argument('--move-output', action='store_true')
parser.add_argument('--screenshot', action='store_true')
parser.add_argument('--fcitx-kimpanel', type=Path)
args = parser.parse_args()
if args.native:
    if not args.extension or not args.addon or not args.server:
        parser.error('--native requires --extension, --addon and --server')
    if args.native == 'firefox' and not args.firefox:
        parser.error('--native firefox requires explicit --firefox binary')
    args.sender = Path(__file__).with_name('native-input.py')
    args.diagnostic_actor = True
    if args.scenario != 'single' and (args.native not in ('gtk', 'qt') or not args.mouse):
        parser.error('--scenario fields/windows requires --native gtk/qt --mouse')
    if args.fallback and (not args.kimpanel or not args.mouse or args.scenario != 'single' or args.native not in ('gtk', 'qt')):
        parser.error('--fallback requires --native gtk/qt --kimpanel --mouse and single scenario')
    if args.move_output and (not args.mouse or args.scenario != 'single' or args.fallback):
        parser.error('--move-output requires --mouse and single scenario without --fallback')
    if args.fcitx_kimpanel and not args.fallback:
        parser.error('--fcitx-kimpanel requires --fallback')
args.output.mkdir(parents=True, exist_ok=True)
base = Path(tempfile.mkdtemp(prefix='shell-', dir=args.output)).resolve()
for name in ('home', 'config', 'data', 'cache', 'state', 'runtime'):
    (base / name).mkdir(mode=0o700)
env = {k: v for k, v in os.environ.items() if k in ('PATH', 'LANG')}
env.update(HOME=str(base/'home'), XDG_CONFIG_HOME=str(base/'config'),
           XDG_DATA_HOME=str(base/'data'), XDG_CACHE_HOME=str(base/'cache'),
           XDG_STATE_HOME=str(base/'state'), XDG_RUNTIME_DIR=str(base/'runtime'),
           XDG_CONFIG_DIRS=str(base/'config'), XDG_DATA_DIRS='/usr/share',
           XDG_CURRENT_DESKTOP='GNOME', XDG_SESSION_TYPE='wayland',
           GSETTINGS_BACKEND='keyfile', GIO_USE_VFS='local', NO_AT_BRIDGE='1',
           DBUS_SYSTEM_BUS_ADDRESS='unix:path=/nonexistent/qingjian-probe',
           QINGJIAN_FIXTURE_ID=str(base))
if args.native:
    env.update(QINGJIAN_TEST_SCENARIO=args.scenario, QINGJIAN_TEST_SCALE=str(args.scale),
        QINGJIAN_TEST_UI_SCALE=str(args.ui_scale), QINGJIAN_TEST_FOLLOW_TEXT='false' if args.no_follow_text else 'true')
    if args.text_scale is not None: env['QINGJIAN_TEST_TEXT_SCALE'] = str(args.text_scale)
    if args.fallback: env['QINGJIAN_TEST_FALLBACK'] = '1'
    if args.move_output: env['QINGJIAN_TEST_MOVE_OUTPUT'] = '1'
    if args.screenshot: env['QINGJIAN_TEST_SCREENSHOT'] = '1'
    if args.fcitx_kimpanel:
        shutil.copy2(args.fcitx_kimpanel.resolve(), base/'kimpanel-fixture.so')
        env['QINGJIAN_TEST_KIMPANEL'] = str(base/'kimpanel-fixture.so')
    shutil.copy2(args.addon.resolve(), base/'qingjian-probe.so')
    env.update(QINGJIAN_TEST_ADDON=str(base/'qingjian-probe.so'),
               QINGJIAN_TEST_SERVER=str(args.server.resolve()), QINGJIAN_TEST_NATIVE=args.native)
    if args.mouse: env['QJ_TEST_MOUSE'] = '1'
    if args.fault: env['QJ_TEST_FAULT'] = args.fault
    if args.firefox: env['QINGJIAN_TEST_FIREFOX'] = str(args.firefox.resolve())
    if args.qt_pythonpath: env['QINGJIAN_TEST_QT_PYTHONPATH'] = str(args.qt_pythonpath.resolve())
if args.extension:
    meta = json.loads((args.extension/'metadata.json').read_text())
    if meta['uuid'] == 'qingjian@qingjian.local':
        env['QINGJIAN_TEST_PRODUCTION'] = '1'
    destination = base/'data'/'gnome-shell'/'extensions'/meta['uuid']
    shutil.copytree(args.extension, destination)
    diagnostics(destination, args.diagnostic_windows, args.diagnostic_actor)
    instrument(destination, args.move_output, args.screenshot)
    enabled = [meta['uuid']]
    if args.kimpanel:
        prepare_kimpanel(args.kimpanel.resolve(), base)
        enabled.append('kimpanel@kde.org')
    settings = base/'config'/'glib-2.0'/'settings'
    settings.mkdir(parents=True)
    features = '["scale-monitor-framebuffer"]' if args.scale != 100 or args.move_output else '[]'
    (settings/'keyfile').write_text('[org/gnome/shell]\nenabled-extensions='+json.dumps(enabled)+
        '\nwelcome-dialog-last-shown-version="50"\n[org/gnome/desktop/interface]\nenable-animations=false\n'
        '[org/gnome/mutter]\nexperimental-features='+features+'\n')
config = base/'dbus.conf'
config.write_text('<busconfig><type>session</type><listen>unix:path='+str(base/'bus')+
    '</listen><auth>EXTERNAL</auth><policy context="default"><allow user="*"/>'+
    '<allow own="*"/><allow send_destination="*"/><allow receive_sender="*"/>'+
    '</policy></busconfig>')
print('Artifacts:', base, flush=True)
processes = []
result = {}
try:
    with (base/'dbus.log').open('w') as log:
        bus = sp.Popen(['dbus-daemon', '--nofork', '--config-file='+str(config)],
                       env=env, stdout=log, stderr=log, start_new_session=True)
        processes.append(bus)
        for _ in range(100):
            if (base/'bus').exists():
                break
            time.sleep(.05)
    env['DBUS_SESSION_BUS_ADDRESS'] = 'unix:path='+str(base/'bus')
    env['DBUS_SYSTEM_BUS_ADDRESS'] = env['DBUS_SESSION_BUS_ADDRESS']
    with (base/'shell.log').open('w') as log:
        monitors = ['--virtual-monitor', '2880x1800', '--virtual-monitor', '1920x1080'] if args.move_output else [
            '--virtual-monitor', '2880x1800' if args.scale != 100 else '1280x720']
        shell = sp.Popen(['gnome-shell', '--headless', '--no-x11', *monitors,
                          '--wayland-display', 'qingjian-test'], env=env,
                         stdout=log, stderr=log, start_new_session=True)
        processes.append(shell)
        for _ in range(100):
            if shell.poll() is not None:
                raise RuntimeError('GNOME 已退出，查看 shell.log')
            check = sp.run(['gdbus', 'call', '--session', '--dest', 'org.gnome.Shell',
                            '--object-path', '/org/gnome/Shell', '--method',
                            'org.gnome.Shell.Extensions.ListExtensions'], env=env,
                           capture_output=True, text=True, timeout=2)
            if check.returncode == 0 and (not args.extension or meta['uuid'] in check.stdout):
                result['extensions'] = check.stdout.strip()
                break
            time.sleep(.1)
        else:
            raise RuntimeError('GNOME D-Bus 服务未就绪')
        if args.sender:
            for _ in range(100):
                check = sp.run(['gdbus', 'call', '--session', '--dest', 'org.freedesktop.DBus',
                                '--object-path', '/org/freedesktop/DBus', '--method',
                                'org.freedesktop.DBus.NameHasOwner',
                                'org.qingjian.Panel1' if env.get('QINGJIAN_TEST_PRODUCTION') else 'org.qingjian.PanelProbe1'],
                               env=env, capture_output=True, text=True, timeout=2)
                if 'true' in check.stdout:
                    break
                time.sleep(.1)
            else:
                raise RuntimeError('探针 D-Bus 服务未就绪')
            sp.run(['gdbus', 'call', '--session', '--dest', 'org.gnome.Shell',
                    '--object-path', '/org/gnome/Shell', '--method',
                    'org.freedesktop.DBus.Properties.Set', 'org.gnome.Shell',
                    'OverviewActive', '<false>'], env=env, capture_output=True, timeout=3, check=True)
            time.sleep(.5)
            sender = sp.Popen((["/usr/bin/python3"] if args.native else []) + [str(args.sender.resolve())],
                              env=env, stdout=sp.PIPE, stderr=sp.PIPE, text=True, start_new_session=True)
            processes.append(sender)
            output, errors = sender.communicate(timeout=45)
            result['sender'] = {'code':sender.returncode, 'stdout':output, 'stderr':errors}
        else:
            result['shell_ready'] = True
except Exception as error:
    result['error'] = str(error)
finally:
    for process in reversed(processes):
        try:
            os.killpg(process.pid, signal.SIGCONT)
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=5)
        except sp.TimeoutExpired:
            pass
        # 组长退出不代表子进程退出（尤其故障注入中暂停的Fcitx）。
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()
    (base/'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n')
    print(json.dumps(result, ensure_ascii=False, indent=2))
    if 'error' in result:
        print((base/'shell.log').read_text()[-6000:])

if 'error' in result or result.get('sender', {}).get('code', 0) != 0:
    raise SystemExit(1)
