"""//! 已删除旧插件映射不能使用磁盘新版哈希；仅实际映射 inode 可作证。"""
import hashlib
import os
from pathlib import Path
import tempfile
from mappings import mapped_plugins

with tempfile.TemporaryDirectory() as temporary:
    base = Path(temporary)
    proc = base / 'proc/1234'
    proc.mkdir(parents=True)
    (proc / 'comm').write_text('fcitx5\n')
    old = base / 'old.so'
    old.write_bytes(b'old plugin')
    current = base / 'qingjian.so'
    current.write_bytes(b'new plugin')
    info = old.stat()
    device = f'{os.major(info.st_dev):02x}:{os.minor(info.st_dev):02x}'
    (proc / 'maps').write_text(f'1000-2000 r-xp 00000000 {device} {info.st_ino} {current} (deleted)\n')
    unknown = mapped_plugins(base / 'proc')[0]
    assert unknown['pid'] == 1234 and unknown['deleted'] and unknown['reload_required']
    assert unknown['mapped_sha256'] is None and unknown['hash_source'] == 'unknown'
    (proc / 'map_files').mkdir()
    (proc / 'map_files/1000-2000').symlink_to(old)
    mapped = mapped_plugins(base / 'proc')[0]
    assert mapped['mapped_sha256'] == hashlib.sha256(b'old plugin').hexdigest()
    assert mapped['mapped_sha256'] != hashlib.sha256(current.read_bytes()).hexdigest()
print('通过：deleted 驻留映射、需重载和实际 inode 哈希')
