#!/usr/bin/env python3
# //! 私有 D-Bus、Xvfb 和 XDG 目录中的 GTK4 自绘/回退验收。
import argparse
import json
import os
from pathlib import Path
import re
import select
import subprocess as sp
import sys
import tempfile
import time

root = Path(__file__).resolve().parents[5]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("case", choices=["keyboard", "mouse", "compositor-loss", "no-compositor"])
parser.add_argument("--server", type=Path, required=True)
parser.add_argument("--addon", type=Path, required=True, help="CMake 构建目录中的 qingjian.so")
parser.add_argument("--xvfb", default="Xvfb")
parser.add_argument("--compositor", default="xcompmgr")
parser.add_argument("--xdotool", default="xdotool")
parser.add_argument("--xwd", help="可选：保存候选窗口 XWD")
parser.add_argument("--artifacts", type=Path, default=root / "target/dual-ui")
parser.add_argument("--without-classicui", action="store_true", help="回退反例：禁用默认 UI，必须失败")
parser.add_argument("--inside", type=Path, help=argparse.SUPPRESS)
args = parser.parse_args()
if args.inside is None:
    from session import launch

    args.artifacts.mkdir(parents=True, exist_ok=True)
    base = Path(tempfile.mkdtemp(prefix="gtk-" + args.case + "-", dir=args.artifacts.resolve()))
    raise SystemExit(launch(base, Path(__file__).resolve(), sys.argv[1:]))

base = args.inside.resolve()
assert os.environ.get("QINGJIAN_FIXTURE_ID") == str(base), "必须通过外层私有 session 启动"
env = os.environ.copy()
env.update(GTK_IM_MODULE="fcitx", QINGJIAN_SOCKET=str(base / "qj.sock"),
           QINGJIAN_RESOURCES=str(root), QINGJIAN_DICT=str(root / "assets/sample/dict.tsv"),
           QINGJIAN_UI_TIMINGS="1")
for directory in ["config/qingjian", "config/fcitx5", "data/fcitx5/addon", "data/fcitx5/inputmethod"]:
    (base / directory).mkdir(parents=True, exist_ok=True)
(base / "config/qingjian/config.toml").write_text(
    '[general]\nlearning_language="en"\n[linux_ui]\nrenderer="qingjian"\n')
(base / "config/fcitx5/config").write_text(
    "[Behavior]\nActiveByDefault=True\nPreloadInputMethod=True\nShowInputMethodInformation=False\n")
(base / "config/fcitx5/conf").mkdir(exist_ok=True)
(base / "config/fcitx5/conf/classicui.conf").write_text(
    "Vertical Candidate List=True\nFont=Sans 10\nTheme=default\n")
(base / "config/fcitx5/profile").write_text(
    "[Groups/0]\nName=Test\nDefault Layout=us\nDefaultIM=qingjian\n"
    "[Groups/0/Items/0]\nName=keyboard-us\n[Groups/0/Items/1]\nName=qingjian\n[GroupOrder]\n0=Test\n")
addon = args.addon.resolve()
assert addon.is_file() and args.server.is_file(), "先构建 Server 和 FFI + X11 addon"
metadata = (root / "apps/linux/fcitx5/data/addon/qingjian.conf").read_text()
(base / "data/fcitx5/addon/qingjian.conf").write_text(
    metadata.replace("Library=qingjian", "Library=" + str(addon.with_suffix(""))))
(base / "data/fcitx5/inputmethod/qingjian.conf").write_text(
    (root / "apps/linux/fcitx5/data/inputmethod/qingjian.conf").read_text())
processes = []


def start(command, name, **kwargs):
    with (base / (name + ".log")).open("w") as log:
        process = sp.Popen(command, env=env, stdout=log, stderr=sp.STDOUT, **kwargs)
    processes.append(process)
    return process


def run(command):
    return sp.check_output(command, env=env, text=True, stderr=sp.STDOUT, timeout=5).strip()


def until(predicate, message, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(.05)
    raise AssertionError(message)


def gtk_window():
    try:
        return run([args.xdotool, "search", "--onlyvisible", "--pid", str(app.pid)]).splitlines()[-1]
    except (sp.CalledProcessError, IndexError):
        return None



def visible(wid):
    try:
        return "Map State: IsViewable" in run(["xwininfo", "-id", wid])
    except sp.CalledProcessError:
        return False


def classic_window():
    tree = run(["xwininfo", "-root", "-tree"])
    ids = re.findall(r'^\s*(0x[0-9a-f]+) "Fcitx5 Input Window": \("fcitx" "fcitx"\)', tree, re.M)
    mapped = [wid for wid in ids if visible(wid)]
    return mapped[0] if len(mapped) == 1 else None


def custom_windows():
    tree = run(["xwininfo", "-root", "-tree"])
    ids = [match[1] for line in tree.splitlines()
           if (match := re.search(r"^\s*(0x[0-9a-f]+) \(has no name\).*? (\d+)x(\d+)\+", line))
           and int(match[2]) > 100 and int(match[3]) > 30 and visible(match[1])]
    return ids


try:
    activatable = run(["dbus-send", "--session", "--print-reply", "--dest=org.freedesktop.DBus",
                       "/org/freedesktop/DBus", "org.freedesktop.DBus.ListActivatableNames"])
    (base / "activatable.txt").write_text(activatable)
    assert set(re.findall(r'string "([^\"]+)"', activatable)) <= {"org.freedesktop.DBus"}, "bus 存在可自动激活的服务"
    read_fd, write_fd = os.pipe()
    try:
        start([args.xvfb, "-displayfd", str(write_fd), "-screen", "0", "800x600x24",
               "-nolisten", "tcp", "-ac"], "xvfb", pass_fds=(write_fd,))
    finally:
        os.close(write_fd)
    try:
        assert select.select([read_fd], [], [], 5)[0], "Xvfb 启动超时"
        display = os.read(read_fd, 100).decode().strip()
        assert display.isdigit(), "Xvfb 未返回 display"
    finally:
        os.close(read_fd)
    env["DISPLAY"] = ":" + display
    compositor = None
    if args.case != "no-compositor":
        compositor = start([args.compositor, "-a"], "compositor")
    start([str(args.server.resolve())], "server")
    ui = "none" if args.without_classicui else "classicui"
    fcitx = start(["fcitx5", "--disable=all",
                   "--enable=keyboard,dbus,dbusfrontend,xcb,qingjian" +
                   (",classicui" if not args.without_classicui else ""),
                   "-u", ui], "fcitx")
    until(lambda: (base / "qj.sock").exists() and
          "Loaded addon qingjian" in (base / "fcitx.log").read_text(), "Fcitx/Server 未就绪")
    app = start([sys.executable, str(Path(__file__).with_name("entry.py")),
                 str(base / "entry.json")], "gtk")
    window = until(gtk_window, "GTK 窗口未就绪")
    run([args.xdotool, "windowfocus", "--sync", window])
    entry_state = json.loads((base / "entry.json").read_text())
    assert entry_state.get("scale") == 1, f"GTK scale 必须为 1，实际 {entry_state.get('scale')}"
    # GTK 的异步 IM context 在窗口显示后初始化；预热后才发测试按键。
    time.sleep(3)
    run(["fcitx5-remote", "-s", "qingjian"])
    run(["fcitx5-remote", "-o"])
    time.sleep(1)
    run([args.xdotool, "type", "--delay", "200", "nihao"])
    candidate = None
    fallback = None
    if compositor is not None:
        until(lambda: "青简 UI 耗时" in (base / "fcitx.log").read_text(), "未使用青简自绘")
        candidate = until(lambda: custom_windows() if len(custom_windows()) == 1 else None,
                          "自绘候选窗口不唯一或不可见")[0]
        assert not classic_window(), "自绘与默认面板同时可见"
        if args.xwd:
            run([args.xwd, "-silent", "-id", candidate, "-out", str(base / "candidate.xwd")])
    if args.case == "compositor-loss":
        compositor.terminate()
        compositor.wait(timeout=5)
        until(lambda: "IsUnMapped" in run(["xwininfo", "-id", candidate]), "自绘窗未撤下", 3)
    if args.case in ("compositor-loss", "no-compositor"):
        fallback = until(classic_window, "默认 Fcitx 候选 UI 未实际显示", 3)
        assert not custom_windows(), "回退时自绘仍然可见"
        (base / "windows-fallback.txt").write_text(run(["xwininfo", "-root", "-tree"]))
        assert json.loads((base / "entry.json").read_text())["text"] == "", "选词前已意外上屏"
        # 固定的独立 ClassicUI 配置：竖排，preedit 后首行的中心。
        focus = run([args.xdotool, "getwindowfocus"])
        run([args.xdotool, "mousemove", "--window", fallback, "40", "42", "click", "1"])
        assert run([args.xdotool, "getwindowfocus"]) == focus, "默认面板点击抢走焦点"
    elif args.case == "mouse":
        focus = run([args.xdotool, "getwindowfocus"])
        run([args.xdotool, "mousemove", "--window", candidate, "70", "74", "click", "1"])
        assert run([args.xdotool, "getwindowfocus"]) == focus, "点击抢走键盘焦点"
    else:
        run([args.xdotool, "key", "space"])
    until(lambda: json.loads((base / "entry.json").read_text())["text"] == "你好", "上屏不正确")
    time.sleep(.5)
    assert json.loads((base / "entry.json").read_text())["text"] == "你好", "重复上屏"
    final_state = json.loads((base / "entry.json").read_text())
    assert final_state.get("scale") == 1
    (base / "result.json").write_text(json.dumps({"case": args.case, "passed": True,
                                                  "display": env["DISPLAY"], "scale": final_state["scale"],
                                                  "fallback_clicked": fallback is not None}))
    print("PASS:", args.case, flush=True)
finally:
    for process in reversed(processes):
        if process.poll() is None:
            process.terminate()
    for process in reversed(processes):
        try:
            process.wait(timeout=5)
        except sp.TimeoutExpired:
            process.kill()
            process.wait()
