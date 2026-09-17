"""//! 不可变回滚快照与原子清单发布；任一步失败恢复旧文件、清单和会话模式。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile


def atomic_copy(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".qingjian-", dir=target.parent)
    os.close(descriptor)
    try:
        shutil.copy2(source, temporary)
        os.replace(temporary, target)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def mode_snapshot():
    """只在用户显式会话安装时保存安装器管理的模式文件，不操作服务。"""
    home = Path.home()
    config = Path(os.environ.get('XDG_CONFIG_HOME', home / '.config'))
    state = Path(os.environ.get('XDG_STATE_HOME', home / '.local/state'))
    unit = 'qingjian-linux-server.service'
    paths = [state / 'qingjian/session.json', config / f'systemd/user/{unit}.d/qingjian-mode.conf',
             config / f'systemd/user/default.target.wants/{unit}',
             config / f'systemd/user/graphical-session.target.wants/{unit}']
    snapshot = []
    for path in paths:
        kind = 'link' if path.is_symlink() else 'file' if path.is_file() else 'absent'
        content = os.readlink(path) if kind == 'link' else path.read_text() if kind == 'file' else None
        snapshot.append({'path': str(path), 'kind': kind, 'content': content})
    return {'uid': os.getuid(), 'files': snapshot}


def restore_mode(snapshot):
    if snapshot is None:
        return
    if snapshot['uid'] != os.getuid():
        raise RuntimeError('会话模式快照用户不匹配')
    allowed = {item['path'] for item in mode_snapshot()['files']}
    for item in snapshot['files']:
        if item['path'] not in allowed:
            raise RuntimeError('会话快照路径不匹配；请使用安装时的 XDG 目录')
    for item in snapshot['files']:
        path = Path(item['path'])
        if path.is_symlink() or path.is_file():
            path.unlink()
        if item['kind'] != 'absent':
            path.parent.mkdir(parents=True, exist_ok=True)
            if item['kind'] == 'link':
                path.symlink_to(item['content'])
            else:
                path.write_text(item['content'])
                path.chmod(0o600)


def relative_path(value):
    relative = Path(value)
    if relative.is_absolute() or '..' in relative.parts:
        raise RuntimeError('回滚清单路径无效')
    return relative


def restore(prefix, records, backup):
    for record in reversed(records):
        relative = relative_path(record['path'])
        target = prefix / relative
        if record['previous']:
            atomic_copy(backup / 'files' / relative, target)
        elif target.is_file():
            target.unlink()


def deploy(stage, prefix, session_state=False):
    files = sorted(path.relative_to(stage) for path in stage.rglob('*') if path.is_file())
    manifest = prefix / 'share/qingjian/install-manifest.json'
    previous_manifest = manifest.read_bytes() if manifest.is_file() else None
    directory = prefix / 'share/qingjian/rollback'
    directory.mkdir(parents=True, exist_ok=True)
    backup = Path(tempfile.mkdtemp(prefix='generation-', dir=directory))
    records, changed = [], []
    snapshot = mode_snapshot() if session_state else None
    try:
        # 完整旧版快照先落地，任何目标校验失败都尚未替换产品文件。
        for relative in files:
            destination = prefix / relative
            if destination.is_symlink() or (destination.exists() and not destination.is_file()):
                raise RuntimeError(f'安装目标不是普通文件：{destination}')
            record = {'path': str(relative), 'previous': destination.exists(),
                      'sha256': hashlib.sha256((stage / relative).read_bytes()).hexdigest()}
            if record['previous']:
                atomic_copy(destination, backup / 'files' / relative)
            records.append(record)
        (backup / 'previous-manifest.json').write_bytes(previous_manifest or b'')
        data = {'version': 2, 'files': records, 'backup': str(backup.relative_to(prefix)), 'mode': snapshot}
        prepared_manifest = backup / 'manifest.json'
        prepared_manifest.write_text(json.dumps(data, indent=2) + '\n')
        # 空目录同样属于产物布局，例如样例包 resources/data/generated。
        for path in stage.rglob('*'):
            if path.is_dir():
                (prefix / path.relative_to(stage)).mkdir(parents=True, exist_ok=True)
        for record in records:
            relative = Path(record['path'])
            changed.append(record)
            atomic_copy(stage / relative, prefix / relative)
        atomic_copy(prepared_manifest, manifest)
    except BaseException:
        restore(prefix, changed, backup)
        # manifest 的发布也属于事务，失败时保留上一次清单和备份。
        # atomic_copy 在 rename 之前失败时原清单未动；发布成功后没有可失败的步骤。
        shutil.rmtree(backup)
        raise
    # 不提前删除旧备份；它仍被上一版清单引用，可继续逐代回滚。


def rollback(prefix):
    manifest = prefix / 'share/qingjian/install-manifest.json'
    data = json.loads(manifest.read_text())
    if data.get('version') != 2:
        raise RuntimeError('旧清单没有完整事务信息，请使用对应版本的回滚工具')
    backup = prefix / relative_path(data['backup'])
    restore_mode(data.get('mode'))
    restore(prefix, data['files'], backup)
    previous = backup / 'previous-manifest.json'
    if previous.stat().st_size:
        atomic_copy(previous, manifest)
    else:
        manifest.unlink()


def main():
    parser = argparse.ArgumentParser(description='青简安装与回滚；不操作用户服务')
    parser.add_argument('--prefix', required=True, type=Path)
    parser.add_argument('--stage', type=Path)
    parser.add_argument('--session-state', action='store_true')
    parser.add_argument('--rollback', action='store_true')
    args = parser.parse_args()
    if args.rollback:
        rollback(args.prefix)
    elif args.stage:
        deploy(args.stage, args.prefix, args.session_state)
    else:
        parser.error('需要 --stage 或 --rollback')


if __name__ == '__main__':
    main()
