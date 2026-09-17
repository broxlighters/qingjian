# //! 在临时副本加入只读几何观察；原始第三方源码由用户显式提供，不纳入仓库。
import hashlib
import json
import shutil
import subprocess


def prepare(source, base):
    destination = base / 'data/gnome-shell/extensions/kimpanel@kde.org'
    shutil.copytree(source, destination)
    metadata = destination / 'metadata.json'
    if not metadata.exists():
        metadata.write_text((destination / 'metadata.json.in').read_text().replace('@localedir@', 'locale'))
    subprocess.run(['glib-compile-schemas', str(destination / 'schemas')], check=True)
    hashes = {str(path.relative_to(source)): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(source.rglob('*')) if path.is_file() and '.git' not in path.parts}
    (base / 'kimpanel-source.json').write_text(json.dumps(hashes, indent=2))
    path = destination / 'extension.js'
    content = path.read_text()
    assert 'TestState()' not in content, '需要未注入的原始 kimpanel 扩展'
    content = content.replace('<interface name="org.kde.impanel">',
        '<interface name="org.kde.impanel"><method name="TestState"><arg type="s" direction="out"/></method>', 1)
    marker = '    updateInputPanel() {'
    assert marker in content
    content = content.replace(marker, '''    TestState() {
        const p=this.inputpanel.panel;
        const items=this.inputpanel.lookupTableLayout.get_children().map(a=>{
            const [x,y]=a.get_transformed_position(); const [w,h]=a.get_transformed_size();
            return {rect:[x,y,w,h],visible:a.visible,mapped:a.mapped};
        });
        return JSON.stringify({visible:p.visible,mapped:p.mapped,items,pointer:global.get_pointer(),
            window:global.display.focus_window?.get_stable_sequence(),
            spot:{x:this.x,y:this.y,width:this.w,height:this.h,relative:this.relative,scale:this.scale}});
    }
''' + marker, 1)
    path.write_text(content)
