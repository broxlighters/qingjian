#!/usr/bin/env python3
# //! 校验固定上游归档，在新目录应用恢复光标补丁；仅产出隔离夹具使用的 addon。
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import tarfile

parser = argparse.ArgumentParser()
parser.add_argument('archive', type=Path)
parser.add_argument('--config-dir', type=Path, required=True, help='同版本上游 CMake configure 生成 config.h 的目录')
parser.add_argument('--output', type=Path, required=True, help='必须尚不存在的新目录')
args = parser.parse_args()
expected = 'a6e4d99a82298845df9f8dc5036c1aaed258c2d3f5bd215b8f91c75410167ff0'
assert hashlib.sha256(args.archive.read_bytes()).hexdigest() == expected
config = args.config_dir.resolve() / 'config.h'
assert '#define FCITX_VERSION_STRING "5.1.19"' in config.read_text()
for package in ('Fcitx5Core', 'Fcitx5Config', 'Fcitx5Utils'):
    assert subprocess.check_output(['pkg-config', '--modversion', package], text=True).strip() == '5.1.19'
args.output.mkdir(parents=True, exist_ok=False)
with tarfile.open(args.archive) as archive:
    archive.extractall(args.output, filter='data')
source = args.output.resolve() / 'fcitx5-5.1.19'
patch = Path(__file__).with_name('fcitx5-5.1.19-kimpanel-resume.patch').resolve()
for flags in (['--check'], [], ['--reverse', '--check']):
    subprocess.run(['git', '-C', str(source), 'apply', *flags, str(patch)], check=True)
assert subprocess.run(['git', '-C', str(source), 'apply', '--check', str(patch)], capture_output=True).returncode != 0
packages = ['Fcitx5Core', 'Fcitx5Config', 'Fcitx5Utils']
cflags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', *packages], text=True))
libs = shlex.split(subprocess.check_output(['pkg-config', '--libs', *packages], text=True))
binary = args.output.resolve() / 'libkimpanel.so'
command = ['c++', '-std=c++20', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror', *cflags,
    '-I' + str(config.parent), *['-I' + str(source / path) for path in ('src/lib', 'src/modules/dbus', 'src/modules/xcb')],
    str(source / 'src/ui/kimpanel/kimpanel.cpp'), *libs, '-o', str(binary)]
subprocess.run(command, check=True)
report = {'archive_sha256': expected, 'patch_sha256': hashlib.sha256(patch.read_bytes()).hexdigest(),
    'config_sha256': hashlib.sha256(config.read_bytes()).hexdigest(), 'command': command,
    'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
    'note': '只供 --fcitx-kimpanel 隔离夹具显式加载；没有安装到系统或青简发行包。'}
(args.output / 'build.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
print(binary)
