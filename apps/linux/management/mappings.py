"""//! 按 /proc 驻留映射 inode 取证；磁盘升级后的文件不能冒充已加载插件。"""
import hashlib
import os
from pathlib import Path


def mapped_plugins(proc=Path('/proc')):
    mappings = []
    for entry in proc.glob('[0-9]*/comm'):
        try:
            if entry.stat().st_uid != os.getuid() or entry.read_text().strip() != 'fcitx5':
                continue
            seen = set()
            for line in (entry.parent / 'maps').read_text().splitlines():
                fields = line.split(maxsplit=5)
                if len(fields) != 6:
                    continue
                deleted = fields[5].endswith(' (deleted)')
                path = fields[5][:-10] if deleted else fields[5]
                if not path.endswith('/qingjian.so'):
                    continue
                key = (fields[3], fields[4], path)
                if key in seen:
                    continue
                seen.add(key)
                inode = int(fields[4])
                major, minor = (int(value, 16) for value in fields[3].split(':'))
                def matches(info):
                    return info.st_ino == inode and os.major(info.st_dev) == major and os.minor(info.st_dev) == minor
                same = False
                try:
                    same = matches(Path(path).stat())
                except OSError:
                    pass
                digest = None
                # 先尝试真实 map_files；若无权限，仅允许读取 inode 完全相同的磁盘文件。
                sources = [entry.parent / 'map_files' / fields[0]]
                if same and not deleted:
                    sources.append(Path(path))
                for source in sources:
                    try:
                        with source.open('rb') as file:
                            if matches(os.fstat(file.fileno())):
                                digest = hashlib.file_digest(file, 'sha256').hexdigest()
                                break
                    except OSError:
                        pass
                mappings.append({'pid': int(entry.parent.name), 'path': path, 'mapped_inode': inode,
                                 'mapped_device': fields[3], 'deleted': deleted,
                                 'reload_required': deleted or not same, 'mapped_sha256': digest,
                                 'hash_source': 'mapped-inode' if digest else 'unknown', 'version': None})
        except (OSError, ValueError):
            pass
    return mappings
