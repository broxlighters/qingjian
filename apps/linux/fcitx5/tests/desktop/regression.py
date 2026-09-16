#!/usr/bin/env python3
# //! GTK 四用例及默认面板缺失反例；参数原样转交 run.py。
import json
import os
from pathlib import Path
import subprocess as sp
import sys

script = Path(__file__).with_name("run.py")

# argparse handles --help before validating required positional/options.  Keep
# the wrapper's help path outside the regression loop so it cannot create
# artifacts or start any of the desktop fixture processes.
if any(argument in ("-h", "--help") for argument in sys.argv[1:]):
    raise SystemExit(sp.run([sys.executable, str(script), *sys.argv[1:]]).returncode)

env = os.environ.copy()
# 注入外部缩放污染，run.py 必须隔离它并记录 GTK 实际值。
env.update(GDK_SCALE="2", GDK_DPI_SCALE="2")
for case, negative in [("keyboard", False), ("mouse", False),
                       ("compositor-loss", False), ("no-compositor", False),
                       ("compositor-loss", True), ("no-compositor", True)]:
    command = [sys.executable, str(script), case, *sys.argv[1:]]
    if negative:
        command.append("--without-classicui")
    result = sp.run(command, env=env, text=True, stdout=sp.PIPE, stderr=sp.STDOUT, timeout=100)
    print(result.stdout, end="", flush=True)
    artifact_line = next(line for line in result.stdout.splitlines() if line.startswith("Artifacts: "))
    base = Path(artifact_line.removeprefix("Artifacts: "))
    assert json.loads((base / "cleanup.json").read_text())["leaked_pids"] == []
    if negative:
        assert result.returncode != 0 and "默认 Fcitx 候选 UI 未实际显示" in result.stdout
        assert not (base / "result.json").exists(), "默认面板缺失不应记录 PASS"
        print("PASS negative:", case, flush=True)
    else:
        assert result.returncode == 0, result.stdout
        state = json.loads((base / "result.json").read_text())
        assert state["passed"] and state["scale"] == 1
        assert state["fallback_clicked"] == (case in ("no-compositor", "compositor-loss"))
