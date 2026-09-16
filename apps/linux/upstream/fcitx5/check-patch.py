#!/usr/bin/env python3
# //! 校验固定归档、两份干净源码重复应用、反向检查与已编译源码一致性。
# SPDX-License-Identifier: LGPL-2.1-or-later
import argparse
import hashlib
from pathlib import Path
import subprocess
import tarfile
import tempfile

EXPECTED = "a6e4d99a82298845df9f8dc5036c1aaed258c2d3f5bd215b8f91c75410167ff0"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("archive", type=Path)
parser.add_argument("--built-source", type=Path, required=True)
args = parser.parse_args()
patch = Path(__file__).with_name("fcitx5-5.1.19-popup-api.patch").resolve()
with args.archive.open("rb") as archive_file:
    assert hashlib.file_digest(archive_file, "sha256").hexdigest() == EXPECTED
paths = [line.removeprefix("+++ b/") for line in patch.read_text().splitlines()
         if line.startswith("+++ b/")]
assert paths and all(path.startswith("src/frontend/waylandim/") for path in paths)
outputs = []
with tempfile.TemporaryDirectory(prefix="fcitx-popup-apply-") as tmp:
    for number in range(2):
        folder = Path(tmp) / str(number)
        folder.mkdir()
        with tarfile.open(args.archive) as archive:
            archive.extractall(folder, filter="data")
        source = folder / "fcitx5-5.1.19"
        command = ["git", "-C", str(source), "apply"]
        subprocess.run([*command, "--check", str(patch)], check=True)
        subprocess.run([*command, str(patch)], check=True)
        subprocess.run([*command, "--reverse", "--check", str(patch)], check=True)
        duplicate = subprocess.run([*command, "--check", str(patch)], capture_output=True)
        assert duplicate.returncode != 0, "重复应用必须被拒绝"
        content = {path: (source / path).read_bytes() for path in paths}
        outputs.append(content)
        assert content == {path: (args.built_source / path).read_bytes() for path in paths}, \
            "patch 与已编译源码不一致"
assert outputs[0] == outputs[1]
print(f"PASS archive SHA256; clean apply x2; reverse check; duplicate rejected; {len(paths)} files match built source")
