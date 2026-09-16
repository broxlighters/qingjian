#!/usr/bin/env python3
"""//! 将 Debian 包信息和可复验的构建信息写入暂存目录。"""
import datetime
import json
import os
import pathlib
import sys


def main() -> None:
    stage, repo, version, architecture, dependencies, commit = sys.argv[1:]
    stage_path = pathlib.Path(stage)
    control_path = stage_path / "DEBIAN/control"
    control = control_path.read_text()
    control = control.replace("${shlibs:Depends}", dependencies, 1)
    control = control.replace("Architecture: any", f"Architecture: {architecture}", 1)
    control = control.replace(
        "Package: qingjian-fcitx5\n",
        f"Package: qingjian-fcitx5\nVersion: {version}\n",
        1,
    )
    control_path.write_text(control)

    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
    built_at = datetime.datetime.fromtimestamp(epoch, datetime.UTC).isoformat().replace("+00:00", "Z")
    info = {
        "package": "qingjian-fcitx5",
        "version": version,
        "architecture": architecture,
        "commit": commit,
        "built_at": built_at,
        "source": "https://github.com/qingjian-team/qingjian",
        "renderer": "fcitx",
        "experimental_x11": False,
    }
    (stage_path / "usr/share/qingjian/resources/build-info.json").write_text(
        json.dumps(info, ensure_ascii=True, indent=2) + "\n"
    )


if __name__ == "__main__":
    main()
