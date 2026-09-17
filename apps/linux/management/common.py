"""//! 管理工具共用的有限命令、XDG 路径与私有 socket 握手。"""
import json
import os
from pathlib import Path
import socket
import struct
import subprocess

UNIT = "qingjian-linux-server.service"
UUID = "qingjian@qingjian.local"
BUS = "org.qingjian.Panel1"


def directory(variable, fallback):
    value = os.environ.get(variable, "")
    return Path(value) if value.startswith("/") else Path.home() / fallback


def config_dir():
    return directory("XDG_CONFIG_HOME", ".config")


def state_dir():
    return directory("XDG_STATE_HOME", ".local/state") / "qingjian"


def command(arguments, timeout=3):
    try:
        result = subprocess.run(arguments, capture_output=True, text=True, timeout=timeout, check=False)
        return result.returncode, result.stdout.strip(), result.stderr.strip()
    except (OSError, subprocess.TimeoutExpired) as error:
        return 127, "", str(error)


def systemctl(*arguments):
    return command(["systemctl", "--user", *arguments])


def read_state():
    try:
        result = json.loads((state_dir() / "session.json").read_text())
        return result if isinstance(result, dict) else {}
    except (OSError, ValueError):
        return {}


def write_state(value):
    path = state_dir() / "session.json"
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    temporary = path.with_suffix(".new")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n")
    temporary.chmod(0o600)
    temporary.replace(path)


def handshake(timeout=1):
    """只创建私密空会话，不发送按键、不读取输入日志或保存学习数据。"""
    runtime = os.environ.get("XDG_RUNTIME_DIR", f"/tmp/qingjian-{os.getuid()}")
    path = os.environ.get("QINGJIAN_SOCKET", str(Path(runtime) / "qingjian.sock"))
    try:
        with socket.socket(socket.AF_UNIX) as connection:
            connection.settimeout(timeout)
            connection.connect(path)
            credentials = connection.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12)
            if struct.unpack("3i", credentials)[1] != os.getuid():
                raise ValueError("socket 用户不匹配")
            request = json.dumps({"OpenSession": {"session": 1, "app": "qingjian-diagnose", "protocol": 4}}).encode()
            connection.sendall(struct.pack("<I", len(request)) + request)
            def receive(length):
                result = bytearray()
                while len(result) < length:
                    part = connection.recv(length - len(result))
                    if not part:
                        raise ValueError("服务未完成握手")
                    result.extend(part)
                return result
            length = struct.unpack("<I", receive(4))[0]
            if not 0 < length <= 262144:
                raise ValueError("握手长度无效")
            reply = json.loads(receive(length))
            if reply.get("Update", {}).get("session") != 1:
                raise ValueError("握手协议不匹配")
            return {"ready": True, "path": path}
    except (OSError, ValueError, KeyError) as error:
        return {"ready": False, "path": path, "reason": str(error)}
