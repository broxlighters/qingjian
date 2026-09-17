"""//! 系统包首次迁移时快照既有账户身份，不访问家目录或用户会话。"""
import argparse
import json
import os
from pathlib import Path
import pwd
import tempfile

STATE = Path('/var/lib/qingjian/package-state.json')


def identity(account):
    return {'uid': account.pw_uid, 'name': account.pw_name, 'home': account.pw_dir}


def record_upgrade(path=STATE, upgrading=False):
    try:
        previous = json.loads(path.read_text())
    except (OSError, ValueError):
        previous = {}
    # 后续升级不扩大集合，升级之后创建的账户仍应走首次登记。
    if previous.get('version') == 2:
        return
    legacy = upgrading or previous.get('upgraded', False)
    users = [identity(account) for account in pwd.getpwall()] if legacy else []
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix='.package-state-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'w') as stream:
            json.dump({'version': 2, 'legacy_users': users}, stream)
            stream.write('\n')
        os.chmod(temporary, 0o644)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def legacy_user(path=STATE):
    try:
        data = json.loads(path.read_text())
        if data.get('version') == 2 and isinstance(data.get('legacy_users'), list):
            return identity(pwd.getpwuid(os.getuid())) in data['legacy_users']
        return data.get('upgraded', True)
    except (OSError, ValueError, KeyError):
        # 没有系统包迁移证据时保留未知既有账户的显式停用。
        return True


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='青简系统包账户迁移记录')
    parser.add_argument('--previous-version', default='')
    record_upgrade(upgrading=bool(parser.parse_args().previous_version))
