# //! 仅复制后的测试扩展安装主动窗口移动/截图入口；发布扩展不包含这些方法。
from pathlib import Path


def install(destination: Path, move: bool, screenshot: bool):
    production = (destination / 'service.js').exists()
    methods = ''
    xml = ''
    if move:
        methods += '''    TestMove(index) {
        const window = global.display.focus_window;
        if (!window || index >= global.display.get_n_monitors()) return false;
        window.move_to_monitor(index);
        const area = Main.layoutManager.getWorkAreaForMonitor(index);
        window.move_frame(true, area.x + 100, area.y + 100);
        return true;
    }
'''
        xml += '<method name="TestMove"><arg type="u" direction="in"/><arg type="b" direction="out"/></method>'
    if screenshot:
        methods += '''    async TestCaptureAsync(_, invocation) {
        const path = GLib.build_filenamev([GLib.get_user_runtime_dir(), '..', 'candidate.png']);
        const stream = Gio.File.new_for_path(path).replace(null, false, Gio.FileCreateFlags.REPLACE_DESTINATION, null);
        try {
            await new Shell.Screenshot().screenshot(false, stream);
            stream.close(null);
            invocation.return_value(new GLib.Variant('(s)', [path]));
        } catch (error) { invocation.return_dbus_error('org.qingjian.TestError', error.message); }
    }
'''
        xml += '<method name="TestCapture"><arg type="s" direction="out"/></method>'
    source = destination / ('service.js' if production else 'extension.js')
    content = source.read_text()
    if screenshot:
        content = "import Shell from 'gi://Shell';\n" + content
    marker = '    constructor() {' if production else '    enable() {'
    source.write_text(content.replace(marker, methods + marker))
    protocol = destination / 'protocol.js'
    protocol.write_text(protocol.read_text().replace('<method name="Hello">', xml + '<method name="Hello">'))


def diagnostics(destination: Path, windows: bool, actor: bool):
    production = (destination / 'service.js').exists()
    if windows:
        source = destination/'extension.js'
        content = source.read_text().replace("() => this._invalidate());", """() => {
            const w = global.display.focus_window;
            const a = w?.get_compositor_private();
            const fr = w?.get_frame_rect();
            const br = w?.get_buffer_rect();
            console.log('QJ_WINDOW '+JSON.stringify(w ? {
                pid:w.get_pid(), wmclass:w.get_wm_class(),
                gtk_app:w.get_gtk_application_id(), client:w.get_client_type(),
                frame:[fr.x,fr.y,fr.width,fr.height], buffer:[br.x,br.y,br.width,br.height],
                actor:[a?.x,a?.y,a?.get_resource_scale()],
            } : null));
            this._invalidate();
        });""", 1)
        source.write_text(content)
    if actor:
        source = destination / ('service.js' if production else 'extension.js')
        if production:
            content = source.read_text().replace('    constructor() {', '''    TestState() {
        const actor = this._surface._actor;
        return JSON.stringify({pointer:global.get_pointer(),
            actor:[actor.x,actor.y,actor.width,actor.height],visible:actor.visible,mapped:actor.mapped,
            window:global.display.focus_window?.get_stable_sequence(),pid:global.display.focus_window?.get_pid(),
            focus:global.display.focus_window?.get_wm_class(),title:global.display.focus_window?.get_title()});
    }
    constructor() {''')
            source.write_text(content)
            surface = destination / 'surface.js'
            surface.write_text(surface.read_text().replace('this._actor.show();',
                "this._actor.show(); console.log('QJ_ACTOR '+JSON.stringify([this._actor.x,this._actor.y,this._actor.width,this._actor.height]));"))
            protocol = destination / 'protocol.js'
            protocol.write_text(protocol.read_text().replace('<method name="Hello">',
                '<method name="TestState"><arg type="s" direction="out"/></method><method name="Hello">'))
            return
        source.write_text(source.read_text().replace('this._actor.show();',
            "this._actor.show(); console.log('QJ_ACTOR '+JSON.stringify([this._actor.x,this._actor.y,this._actor.width,this._actor.height]));")
            .replace('    enable() {', """    TestState() {
        return JSON.stringify({pointer:global.get_pointer(),
            actor:[this._actor.x,this._actor.y,this._actor.width,this._actor.height],
            visible:this._actor.visible, mapped:this._actor.mapped,
            window:global.display.focus_window?.get_stable_sequence(),pid:global.display.focus_window?.get_pid(),
            focus:global.display.focus_window?.get_wm_class(),title:global.display.focus_window?.get_title()});
    }
    enable() {"""))
        protocol = destination/'protocol.js'
        protocol.write_text(protocol.read_text().replace('<method name="Hello">',
            '<method name="TestState"><arg type="s" direction="out"/></method><method name="Hello">'))
