"""//! 默认只读诊断；只采集白名单字段，不读取历史输入或完整环境。"""
import argparse
import json
import os
from pathlib import Path
import sys
from common import BUS, UNIT, UUID, command, config_dir, handshake, read_state, systemctl
from mappings import mapped_plugins


def report(prefix):
    properties = ["ActiveState", "SubState", "UnitFileState", "Result", "MainPID", "FragmentPath",
                  "DropInPaths", "ExecMainStatus", "NRestarts", "StartLimitBurst", "StartLimitIntervalUSec",
                  "InvocationID", "PartOf", "ExecStart"]
    code, output, error = systemctl("show", UNIT, "--property=" + ",".join(properties))
    service = dict(line.split("=", 1) for line in output.splitlines() if "=" in line)
    if code:
        service["error"] = error
    _, linger, _ = command(["loginctl", "show-user", str(os.getuid()), "-p", "Linger", "--value"])
    versions = {}
    for name, arguments in {"server": [str(prefix / "bin/qingjian-linux-server"), "--version"],
                            "fcitx": ["fcitx5", "--version"], "shell": ["gnome-shell", "--version"]}.items():
        _, output, _ = command(arguments)
        versions[name] = output or None
    _, extension, _ = command(["gnome-extensions", "info", UUID])
    _, owner, _ = command(["gdbus", "call", "--session", "--dest", "org.freedesktop.DBus",
                           "--object-path", "/org/freedesktop/DBus", "--method",
                           "org.freedesktop.DBus.NameHasOwner", BUS])
    # 扩展状态与软件 Painted 分开；未产生实际帧时绝不把 owner 当作显示成功。
    _, panel, _ = command(["gdbus", "call", "--session", "--dest", BUS,
                           "--object-path", "/org/qingjian/Panel1", "--method", BUS + ".Status"])
    mappings = mapped_plugins()
    backends = []
    runtime = Path(os.environ.get('XDG_RUNTIME_DIR', f'/tmp/qingjian-{os.getuid()}'))
    for pid in {item['pid'] for item in mappings}:
        try:
            path = runtime / f'qingjian-ui-{pid}.json'
            if path.stat().st_uid == os.getuid() and path.stat().st_size <= 16384:
                backends.append(json.loads(path.read_text()))
        except (OSError, ValueError):
            pass
    return {"versions": versions, "service": service, "startup": read_state(), "linger": linger or None,
            "socket": handshake(), "mapped_plugins": mappings, "backends": backends, "prefix": str(prefix),
            "extension": {"uuid": UUID, "info": extension or None, "owner": owner == "(true,)",
                          "panel_status": panel or None},
            "configuration": str(config_dir() / "qingjian/config.toml")}


def main():
    parser = argparse.ArgumentParser(description="青简只读诊断")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--prefix", default=str(Path(__file__).resolve().parents[3]))
    args = parser.parse_args()
    data = report(Path(args.prefix))
    if args.json:
        print(json.dumps(data, ensure_ascii=False, indent=2))
    else:
        for key, value in data.items():
            print(f"{key}: {json.dumps(value, ensure_ascii=False)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
