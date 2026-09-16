# //! 先隔离环境再启动私有 bus；不加载任何自动激活 service 目录。
import json
import os
from pathlib import Path
import signal
import subprocess as sp
import sys
import time
from xml.sax.saxutils import escape


def launch(base, script, arguments):
    for directory in ("config", "data", "state", "cache", "runtime", "home"):
        (base / directory).mkdir(mode=0o700)
    # 不继承真实用户的 XDG、Fcitx、D-Bus、GTK 配置覆盖。
    env = {key: os.environ[key] for key in ("PATH", "LD_LIBRARY_PATH") if key in os.environ}
    env.update(HOME=str(base / "home"), LANG="C.UTF-8", LC_ALL="C.UTF-8",
               XDG_CONFIG_HOME=str(base / "config"), XDG_DATA_HOME=str(base / "data"),
               XDG_STATE_HOME=str(base / "state"), XDG_CACHE_HOME=str(base / "cache"),
               XDG_RUNTIME_DIR=str(base / "runtime"), XDG_CONFIG_DIRS=str(base / "config"),
               XDG_DATA_DIRS="/usr/local/share:/usr/share", XDG_SESSION_TYPE="x11",
               XDG_CURRENT_DESKTOP="QingjianTest", GIO_USE_VFS="local", GTK_USE_PORTAL="0",
               NO_AT_BRIDGE="1", GTK_A11Y="none", GDK_BACKEND="x11", GDK_SCALE="1",
               GDK_DPI_SCALE="1", DBUS_SYSTEM_BUS_ADDRESS="unix:path=/nonexistent/qingjian-test",
               QINGJIAN_FIXTURE_ID=str(base))
    # 不 include session.conf，不声明 servicedir/standard_session_servicedirs，
    # 不启用 systemd activation。bus 无从激活 portal/GVfs/keyring。
    config = base / "dbus.conf"
    config.write_text(
        '<busconfig><type>session</type><listen>unix:path=' + escape(str(base / "bus")) +
        '</listen><auth>EXTERNAL</auth><policy context="default">'
        '<allow user="*"/><allow own="*"/><allow send_destination="*"/>'
        '<allow receive_sender="*"/></policy></busconfig>')
    print("Artifacts:", base, flush=True)
    command = ["dbus-run-session", "--config-file=" + str(config), "--",
               sys.executable, str(script), *arguments, "--inside", str(base)]
    process = sp.Popen(command, env=env, start_new_session=True)
    try:
        code = process.wait(timeout=90)
    finally:
        # 清理仅属于本次测试的新进程组；不触及真实桌面服务。
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=5)
        except sp.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
        token = ("QINGJIAN_FIXTURE_ID=" + str(base)).encode()
        leaked = []
        for _ in range(50):
            leaked = []
            for path in Path("/proc").glob("[0-9]*/environ"):
                try:
                    if token in path.read_bytes().split(b"\0"):
                        leaked.append(int(path.parent.name))
                except (OSError, PermissionError):
                    pass
            if not leaked:
                break
            time.sleep(.05)
        (base / "cleanup.json").write_text(json.dumps({"leaked_pids": leaked}))
        if leaked:
            (base / "result.json").unlink(missing_ok=True)
        assert not leaked, f"测试进程泄漏: {leaked}"
    return code
