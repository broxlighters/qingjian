"""//! 幂等用户会话登记；同名服务互斥模式，不修改 linger 或 Fcitx profile。"""
import argparse
import json
import os
from pathlib import Path
import sys
import time
from common import UNIT, UUID, command, config_dir, handshake, read_state, state_dir, systemctl, write_state
from package_state import legacy_user

MARKER = "# Managed by qingjian-session-setup\n"


def checked(*arguments):
    code, output, error = systemctl(*arguments)
    if code:
        raise RuntimeError(error or output or "用户服务管理器不可用")
    return output


def installed_unit(prefix):
    choices = [prefix / "lib/systemd/user" / UNIT, prefix / "share/systemd/user" / UNIT]
    return next((path for path in choices if path.is_file()), None)


def preflight(prefix):
    """只自动迁移已知链接；未知 override 和混合来源必须先由用户处理。"""
    conflicts = []
    directory = config_dir() / "systemd/user"
    override = directory / UNIT
    unit = installed_unit(prefix)
    if override.exists() and (not override.is_symlink() or override.resolve() != unit):
        conflicts.append(str(override))
    for dropin in (directory / f"{UNIT}.d").glob("*.conf"):
        if not (dropin.name == "qingjian-mode.conf" and dropin.read_text().startswith(MARKER)):
            conflicts.append(str(dropin))
    recognized = {str(path) for base in [prefix, Path('/usr'), Path.home() / '.local']
                  for path in [base / 'lib/systemd/user' / UNIT, base / 'share/systemd/user' / UNIT]}
    for target in ['default.target', 'graphical-session.target']:
        link = directory / f'{target}.wants' / UNIT
        if (link.exists() or link.is_symlink()) and (not link.is_symlink() or str(link.resolve()) not in recognized):
            conflicts.append(str(link))
    for other in [Path("/usr"), Path.home() / ".local"]:
        if other == prefix:
            continue
        for suffix in ["bin/qingjian-linux-server", "share/fcitx5/addon/qingjian.conf"]:
            if (other / suffix).exists():
                conflicts.append(str(other / suffix))
    if os.environ.get('DBUS_SESSION_BUS_ADDRESS'):
        _, dropins, _ = systemctl('show', UNIT, '-p', 'DropInPaths', '--value')
        for entry in dropins.split():
            path = Path(entry)
            if path == directory / f'{UNIT}.d/qingjian-mode.conf':
                continue
            if entry not in conflicts:
                conflicts.append(entry)
    return conflicts


def configure(prefix, mode, enabled):
    unit = installed_unit(prefix)
    if not unit:
        raise RuntimeError("未找到随包服务文件")
    directory = config_dir() / "systemd/user"
    directory.mkdir(parents=True, exist_ok=True)
    dropin = directory / f"{UNIT}.d/qingjian-mode.conf"
    if mode == "background":
        dropin.parent.mkdir(parents=True, exist_ok=True)
        temporary = dropin.with_suffix(".new")
        temporary.write_text(MARKER + "[Unit]\nPartOf=\n")
        temporary.replace(dropin)
    elif dropin.exists():
        dropin.unlink()
    # 只接受目标文件名匹配且确实指向青简单元的链接，绝不删除普通文件。
    for target in ["default.target", "graphical-session.target"]:
        link = directory / f"{target}.wants" / UNIT
        if link.is_symlink() and link.resolve().name == UNIT:
            link.unlink()
        elif link.exists():
            raise RuntimeError(f"启用目标存在未知文件：{link}")
    if enabled:
        target = "default.target" if mode == "background" else "graphical-session.target"
        link = directory / f"{target}.wants" / UNIT
        link.parent.mkdir(parents=True, exist_ok=True)
        link.symlink_to(unit)
    checked("daemon-reload")


def setup(args):
    prefix = Path(args.prefix).resolve()
    if not os.environ.get("DBUS_SESSION_BUS_ADDRESS"):
        raise RuntimeError("没有用户会话总线；文件已安装，登录 GNOME 后再登记")
    if args.login and "GNOME" not in os.environ.get("XDG_CURRENT_DESKTOP", "").upper().split(":"):
        raise RuntimeError("当前不是 GNOME 会话，不登记服务")
    conflicts = preflight(prefix)
    if conflicts:
        raise RuntimeError("请先处理混合安装或未知 override，未修改服务：" + ", ".join(conflicts))
    state = read_state()
    try:
        options = json.loads((prefix / 'share/qingjian/install-options.json').read_text())
    except (OSError, ValueError):
        options = {}
    package_upgrade = prefix == Path('/usr') and legacy_user()
    # 用户源码安装首次登记登录入口；已有覆盖（包括 Hidden=true）绝不重写。
    autostart = config_dir() / "autostart/qingjian-session-setup.desktop"
    supplied = prefix / "etc/xdg/autostart/qingjian-session-setup.desktop"
    if not state.get("initialized") and supplied.is_file() and not autostart.exists():
        autostart.parent.mkdir(parents=True, exist_ok=True)
        autostart.symlink_to(supplied)
    mode = args.startup or state.get("mode") or options.get('startup') or "session"
    if mode == "background":
        _, linger, _ = command(["loginctl", "show-user", str(os.getuid()), "-p", "Linger", "--value"])
        if linger != "yes":
            raise RuntimeError("后台模式要求 Linger=yes。请自行决定是否运行 loginctl enable-linger；它影响全部用户服务")
    _, status, _ = systemctl("is-enabled", UNIT)
    if status in {"masked", "masked-runtime"}:
        print("青简服务已被屏蔽，保留用户选择")
        return 0
    was_installed = state.get('initialized') or options.get('previous_install') or package_upgrade
    enabled = not (was_installed and status not in {"enabled", "enabled-runtime", "linked", "linked-runtime"})
    if args.enable:
        enabled = True
    if args.disable:
        enabled = False
    active = systemctl("is-active", UNIT)[1] == "active"
    # 同一个服务原地切换，不停止其他图形服务；显式停用保持停用。
    if active and (not enabled or mode != state.get("mode", "session")):
        checked("stop", UNIT)
    configure(prefix, mode, enabled)
    graphical = systemctl("is-active", "graphical-session.target")[1] == "active"
    ready = {"ready": False, "reason": "尚未进入图形会话"}
    should_start = enabled and (mode == "background" or graphical)
    if should_start:
        if args.recover:
            checked("reset-failed", UNIT)
        checked("restart" if args.restart and active else "start", UNIT)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            ready = handshake(min(0.2, max(0.01, deadline - time.monotonic())))
            if ready["ready"]:
                break
            # socket 就绪有界轮询，不猜测 Shell/Fcitx 启动顺序。
            time.sleep(0.05)
    extension = 'pending' if args.enable_extension else state.get("extension", "pending")
    if (prefix / "share/gnome-shell/extensions" / UUID).is_dir():
        if extension == 'pending' and not args.enable_extension:
            _, disabled, _ = command(['gnome-extensions', 'list', '--disabled'])
            _, explicitly_disabled, _ = command(['gsettings', 'get', 'org.gnome.shell', 'disabled-extensions'])
            # 服务的旧用户快照不代表扩展曾安装；新扩展默认 disabled 仍需首次登记。
            legacy_disabled = not state.get('initialized') and options.get('previous_extension') and UUID in disabled.splitlines()
            if UUID in explicitly_disabled or legacy_disabled:
                extension = 'preserved-disabled'
        if extension == "pending" or args.enable_extension:
            code, _, _ = command(["gnome-extensions", "info", UUID])
            if code:
                print("扩展尚未被 Shell 发现，请重新登录；下次登录会检查停用选择后继续登记")
            else:
                code, _, error = command(["gnome-extensions", "enable", UUID])
                if code:
                    print("扩展启用失败：" + error)
                else:
                    extension = "registered"
    write_state({"initialized": True, "version": 1, "prefix": str(prefix), "mode": mode,
                 "enabled": enabled, "extension": extension})
    print(json.dumps({"mode": mode, "enabled": enabled, "socket": ready, "extension": extension}, ensure_ascii=False))
    return 0 if not should_start or ready["ready"] else 1


def main():
    parser = argparse.ArgumentParser(description="青简会话登记；不修改 linger，不重启 Shell/Fcitx")
    parser.add_argument("--prefix", default=str(Path(__file__).resolve().parents[3]))
    parser.add_argument("--startup", choices=["session", "background"])
    parser.add_argument("--login", action="store_true")
    choice = parser.add_mutually_exclusive_group()
    choice.add_argument("--enable", action="store_true")
    choice.add_argument("--disable", action="store_true")
    parser.add_argument("--enable-extension", action="store_true")
    parser.add_argument("--recover", action="store_true")
    parser.add_argument("--restart", action="store_true")
    args = parser.parse_args()
    try:
        return setup(args)
    except (RuntimeError, OSError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
