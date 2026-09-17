"""//! 故障注入验证部署原子性、旧回滚链和用户模式快照。"""
import importlib.util
import json
import os
from pathlib import Path
import tempfile

spec = importlib.util.spec_from_file_location('deploy', Path(__file__).parents[1] / 'scripts/deploy.py')
deploy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deploy)

with tempfile.TemporaryDirectory() as temporary:
    base = Path(temporary)
    os.environ.update(HOME=str(base / 'home'), XDG_CONFIG_HOME=str(base / 'config'), XDG_STATE_HOME=str(base / 'state'))
    stage, prefix = base / 'stage', base / 'prefix'
    (stage / 'bin').mkdir(parents=True)
    (stage / 'resources/data/generated').mkdir(parents=True)
    for name in ['a', 'b']:
        (stage / 'bin' / name).write_text('v1')
    deploy.deploy(stage, prefix, True)
    assert (prefix / 'resources/data/generated').is_dir()
    state = base / 'state/qingjian/session.json'
    state.parent.mkdir(parents=True)
    state.write_text('{"mode":"session","enabled":false}')
    for name in ['a', 'b']:
        (stage / 'bin' / name).write_text('v2')
    deploy.deploy(stage, prefix, True)
    manifest = prefix / 'share/qingjian/install-manifest.json'
    old_manifest = manifest.read_bytes()
    backup = prefix / json.loads(old_manifest)['backup']
    backup_files = {str(path.relative_to(backup)): path.read_bytes() for path in backup.rglob('*') if path.is_file()}
    for name in ['a', 'b']:
        (stage / 'bin' / name).write_text('v3')
    original = deploy.atomic_copy
    for target in [prefix / 'bin/b', manifest]:
        failed = [False]
        def copy(source, destination):
            if destination == target and not failed[0]:
                failed[0] = True
                raise OSError('injected publication failure')
            original(source, destination)
        deploy.atomic_copy = copy
        try:
            deploy.deploy(stage, prefix, True)
            raise AssertionError('故障注入未触发')
        except OSError:
            pass
        finally:
            deploy.atomic_copy = original
        assert manifest.read_bytes() == old_manifest
        assert all((prefix / 'bin' / name).read_text() == 'v2' for name in ['a', 'b'])
        assert backup_files == {str(path.relative_to(backup)): path.read_bytes() for path in backup.rglob('*') if path.is_file()}
    # 失败后的旧回滚仍可用，且模式恢复以旧 session.json 为准。
    state.write_text('{"mode":"background","enabled":true}')
    link = base / 'config/systemd/user/default.target.wants/qingjian-linux-server.service'
    link.parent.mkdir(parents=True)
    link.symlink_to(prefix / 'share/systemd/user/qingjian-linux-server.service')
    deploy.rollback(prefix)
    assert state.read_text() == '{"mode":"session","enabled":false}'
    assert not link.is_symlink()
    assert all((prefix / 'bin' / name).read_text() == 'v1' for name in ['a', 'b'])
    deploy.rollback(prefix)
    assert not state.exists() and not manifest.exists() and not (prefix / 'bin/a').exists()
print('通过：部署中断、manifest 失败、空目录、多代回滚和会话模式恢复')
