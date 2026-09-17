#!/usr/bin/env python3
"""//! 隔离用户管理器夹具：模式迁移、停用保留、linger门禁、诊断握手和回滚。"""
import importlib.util
import json
import os
import runpy
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import threading
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[3]
MANAGEMENT = ROOT / "apps/linux/management"
sys.path.insert(0, str(MANAGEMENT))
from common import handshake
from session import configure, installed_unit
import session


def socket_server(path):
    path.parent.mkdir(mode=0o700, exist_ok=True)
    server = socket.socket(socket.AF_UNIX)
    server.bind(str(path))
    server.listen()
    def serve():
        with server:
            connection, _ = server.accept()
            with connection:
                length = struct.unpack("<I", connection.recv(4))[0]
                json.loads(connection.recv(length))
                body = json.dumps({"Update": {"session": 1}}).encode()
                connection.sendall(struct.pack("<I", len(body)) + body)
    thread = threading.Thread(target=serve)
    thread.start()
    return thread


with tempfile.TemporaryDirectory() as temporary:
    base = Path(temporary)
    home = base / "home"
    prefix = base / "prefix"
    config = base / "config"
    runtime = base / "runtime"
    for directory in [home, config, runtime]:
        directory.mkdir(mode=0o700)
    unit = prefix / "share/systemd/user/qingjian-linux-server.service"
    unit.parent.mkdir(parents=True)
    unit.write_text("[Install]\nWantedBy=graphical-session.target\n")
    fake = base / "bin/systemctl"
    fake.parent.mkdir()
    log = base / "systemctl.log"
    fake.write_text(f"#!/bin/sh\nprintf '%s\\n' \"$*\" >> {log}\nexit 0\n")
    fake.chmod(0o755)
    os.environ.update(HOME=str(home), XDG_CONFIG_HOME=str(config), XDG_RUNTIME_DIR=str(runtime),
                      PATH=str(fake.parent) + ":" + os.environ["PATH"])
    configure(prefix, "session", True)
    session_link = config / "systemd/user/graphical-session.target.wants/qingjian-linux-server.service"
    assert session_link.resolve() == unit
    configure(prefix, "background", True)
    assert not session_link.exists()
    assert (config / "systemd/user/default.target.wants/qingjian-linux-server.service").resolve() == unit
    dropin = config / "systemd/user/qingjian-linux-server.service.d/qingjian-mode.conf"
    assert dropin.read_text().startswith("# Managed by qingjian-session-setup")
    configure(prefix, "session", False)
    assert not dropin.exists()
    assert installed_unit(prefix) == unit
    thread = socket_server(runtime / "qingjian.sock")
    assert handshake()["ready"]
    thread.join()
    stage = base / "stage"
    (stage / "bin").mkdir(parents=True)
    (stage / "bin/app").write_text("new")
    (prefix / "bin").mkdir(parents=True, exist_ok=True)
    (prefix / "bin/app").write_text("old")
    subprocess.run([sys.executable, str(ROOT / "apps/linux/scripts/deploy.py"), "--stage", str(stage),
                    "--prefix", str(prefix)], check=True)
    assert (prefix / "bin/app").read_text() == "new"
    subprocess.run([sys.executable, str(ROOT / "apps/linux/scripts/deploy.py"), "--rollback",
                    "--prefix", str(prefix)], check=True)
    assert (prefix / "bin/app").read_text() == "old"
    # 不调用真实 user manager；将状态响应固定，验证入口保留停用和显式模式选择。
    os.environ.update(XDG_STATE_HOME=str(base / 'state'), DBUS_SESSION_BUS_ADDRESS='unix:path=/private-test',
                      XDG_CURRENT_DESKTOP='GNOME')
    calls = []
    enabled_state = 'disabled'
    def manager(*arguments):
        calls.append(arguments)
        if arguments[0] == 'is-enabled':
            return 1, enabled_state, ''
        if arguments[0] == 'is-active':
            return 0, 'active' if arguments[1] == 'graphical-session.target' else 'inactive', ''
        return 0, '', ''
    session.systemctl = manager
    session.preflight = lambda _: []
    session.handshake = lambda _: {'ready': True}
    session.command = lambda _: (0, 'no', '')
    args = SimpleNamespace(prefix=str(prefix), login=True, startup=None, enable=False,
                           disable=False, recover=False, restart=False, enable_extension=False)
    assert session.setup(args) == 0
    assert ('start', session.UNIT) in calls
    calls.clear()
    assert session.setup(args) == 0
    assert ('start', session.UNIT) not in calls  # 登记后明确 disable 保留。
    args.startup = 'background'
    try:
        session.setup(args)
        raise AssertionError('没有 linger 不应启用后台模式')
    except RuntimeError as error:
        assert 'Linger=yes' in str(error)
    session.command = lambda _: (0, 'yes', '')
    args.enable = True
    assert session.setup(args) == 0
    assert (config / 'systemd/user/default.target.wants' / session.UNIT).exists()
    assert not (config / 'systemd/user/graphical-session.target.wants' / session.UNIT).exists()
print("通过：服务模式迁移、socket诊断和安装回滚")
for test in ['transaction.py', 'migration.py', 'registration.py', 'resident.py']:
    runpy.run_path(str(Path(__file__).with_name(test)), run_name='__main__')
