# GNOME 阶段 0 位图通路探针

本目录包含v1固定色块传输探针与v2真实候选实验。v1验证密封memfd、GJS异步读取、`St.ImageContent`和绘制回执，固定显示在stage `(40,80)`；v2经已证明的dbus输入来源与窗口凭证显示共享渲染器真实候选，覆盖GTK/Qt多框多窗、分数倍率和跨输出重绘。两者均独立UUID/服务名，不进入普通构建，`shell-version=50`只声明实验目标API，不代表正式支持或阶段0全通过。

```sh
cmake -S apps/linux/probes/gnome -B target/gnome-probe
cmake --build target/gnome-probe
python3 apps/linux/probes/gnome/run-shell.py --extension apps/linux/probes/gnome/extension --sender target/gnome-probe/qingjian-gnome-probe
```

将 `extension/` 全部文件安装到隔离 GNOME 50 会话的 `$XDG_DATA_HOME/gnome-shell/extensions/qingjian-probe@qingjian.local/`，在启动 Shell 前启用该 UUID，随后在同一 D-Bus 会话运行 `target/gnome-probe/qingjian-gnome-probe`。当前 GNOME Wayland 会话不能原地发现新的扩展时，应在专用用户下重新登录，不调用已废弃的 `ReloadExtension`，也不重启用户 Shell。

探针只显示一秒以内的有界测试色块；发送方退出、焦点切换、Overview、会话变化或超时均清除位图。发送方完成 `Painted` 后发送 `Hide` 并退出。这里的 `Painted` 是 mapped Actor 存在时 stage `after-paint` 的软件里程碑，不证明物理屏幕呈现。截图需要另行固定样例与捕获时序。探针不记录或落盘输入内容。`node apps/linux/probes/gnome/test.mjs` 验证重复 Show/Hide、晚到回调与卸载清理，不能替代真实 GJS。

后续门槛：真实浏览器、GTK、Qt 输入上下文与焦点绑定、光标随动、真实候选点击、100%/150%/约 167% 缩放、断线回退；证据未齐时生产选择器仍保留默认 Fcitx。

## 真实候选实验

以下只用于隔离会话。构建 Fcitx 插件时显式添加 `-DQINGJIAN_GNOME_PROBE=ON -DQINGJIAN_RENDER_FFI=ON -DQINGJIAN_X11_BACKEND=OFF`，指定当前 render-ffi archive。普通安装脚本不启用该开关。

启动隔离 Fcitx 前设置 `QINGJIAN_GNOME_PROBE=1`、`QINGJIAN_UI_DIAGNOSTICS=1`，测试配置使用 `renderer="qingjian"`。实验 v2 只接收 `dbus` frontend、`wayland:` display、`RelativeRect` 坐标；不再以 program/wmclass 对应窗口。插件在真实 `ObjectVTableBase::currentMessage()` 调用栈内复制已校验 sender 的消息路径、IC UUID 和按键元数据，经实际 frontend 总线的 GetId 与 Shell session daemon 校对。Shell 在具体窗口 `notify::user-time` 同步回调取得相同时间/键码的真实 KEY_PRESS，再以总线提供的 sender PID作辅证。凭证绑定真实 Window 对象、Shell 焦点代次、IC UUID/代次和已握手插件连接；PID 本身不构成窗口证明。

Shell只在内存保留最多500ms/32条的短命物理键码和时间证据，不写日志或磁盘，不保存keysym或输入文本。证据单次签发，歧义和迟到拒绝；同来源在途 Bind 可以合并，跨 IC/epoch/owner 不得复用。Withdraw 只撤当前帧、保留有效窗口凭证；Hide、失焦、Reset、能力变化、点击应用、Overview、销毁、期限到期撤销凭证。未验证的前端和跨总线一律默认 Fcitx，不把 IBus 视作已支持。

GTK/Qt 单框、同窗双框、同 PID 双 mapped 窗口 A→B→A 已有隔离鼠标闭环；Qt 保留 program=`python3.14` 与 wmclass=`qingjian-qt-test` 的真实差异。100%/150%/约167% 和系统 portal 文字倍率有组件结果，见工程记录。跨输出/工作区变化现在保留同窗有效凭证并发GeometryChanged，新submission重读cursor并重新栅格化；GTK/Qt167%→100%→167%无新键双向通过。连续几何准备共用250ms截止，Bind不延长proof。该实验仍每帧隐藏再准备，不声称连续输入无闪烁、正式geometry协议和完整生命周期验收通过。指针接收区域还是整张位图，正式版需收紧为交互命中区域。每100ms至多一个续约，500ms租约；屏幕键盘和隐私仍优先。

测试命令：

```sh
node apps/linux/probes/gnome/test.mjs
ctest --test-dir target/gnome-candidate-probe -R qingjian-gnome --output-on-failure
```

组件假扩展覆盖绘制回执丢失、续约和 Hide 不回应，确保不会因 100ms tick 反复取消 250ms 超时而永久压住默认 UI。真实桌面仍按方案单独验收。

隔离真实输入/鼠标夹具现在也在仓库内；启动器复制已构建 `.so`，随后再启动子进程，避免链接过程与测试抢同一文件。所有输入/故障注入仅发送到启动器自己的私有 D-Bus 和 headless Shell；`native-input.py` 拒绝普通用户会话环境。

```sh
cargo build -p qingjian-linux-server --release --locked
python3 apps/linux/probes/gnome/run-shell.py --extension apps/linux/probes/gnome/extension --native gtk --mouse --addon target/gnome-candidate-probe/qingjian.so --server target/release/qingjian-linux-server
# --fault pause / exit / overview 代替 --mouse 可测试 Actor 自清理。
# --native qt --qt-pythonpath 路径：真实Qt自绘鼠标闭环。
# --scenario fields / windows --mouse：两个输入框或同进程双mapped窗口A→B→A。
# --scale 100 / 150 / 167：Mutter临时输出配置，保存GetCurrentState，非环境变量模拟。
# --text-scale 1.25 [--no-follow-text]：显式启动系统真实GNOME portal，验证文字倍率及关闭跟随。
# --ui-scale 125 / 150：真实Server配置路径。
# --fallback --kimpanel /原始kimpanel源码 --native gtk --mouse：禁用青简后默认面板可见且鼠标选词，再启用扩展输入并自绘选词。
# --move-output --mouse：两个真实临时输出167%→100%→167%，无新按键重绘并最终选词。
# --screenshot --mouse：仅临时扩展注入Shell.Screenshot，输出candidate.png。
# --fcitx-kimpanel /实验/libkimpanel.so：显式隔离加载修复resume光标的上游补丁（配--fallback）。
# --native firefox --firefox /实际/Firefox二进制 可测固定网页；记录包来源，裸Snap内二进制不等于Snap沙箱验收。
```

默认恢复夹具需要显式提供上游 kimpanel 源码，临时副本仅加只读几何 TestState，生成 metadata/编译schemas并记录源文件SHA256；第三方源码不纳入本仓库。默认恢复只支持 GTK/Qt 单框。测试只输入固定“nihao”，结果精确校验“你好”、默认恢复再接管的“你好你好”或两框累计值。原装Fcitx Kimpanel恢复位置仍错误；仅显式上游补丁补足定位，不将它暗中作为产品依赖。构建见[上游记录](../../upstream/fcitx5/README.md#kimpanel-恢复光标的独立实验补丁)。

`--native` 模式只在临时复制的扩展内注入 `TestState` 诊断读取 Actor/焦点/固定测试页面标题，源码扩展与生产插件不暴露该入口。退出删除的是子进程，测试日志保留在 `--output`（默认 `target/gnome-stage0`）。不要同时重建传入 `.so` 和开始复制；先等待构建成功。

## 专用真实会话的后续装载

隔离组件通过后，下一轮需专用测试用户的真实 GNOME Wayland 登录；不要用当前工作会话自动重启 Shell 或切换输入法。

1. 在专用测试用户下，将本目录 `extension/` 复制为 `~/.local/share/gnome-shell/extensions/qingjian-probe@qingjian.local/`。记录 Shell 小版本、插件/扩展哈希、应用版本与启动参数。
2. 保存工作后正常注销并重新登录 GNOME Wayland，让 Shell 发现新 UUID；执行 `gnome-extensions info qingjian-probe@qingjian.local`，随后 `gnome-extensions enable qingjian-probe@qingjian.local`。不调用废弃的 `ReloadExtension`，不启用 Shell unsafe-mode。
3. 在该专用用户环境部署显式 `QINGJIAN_GNOME_PROBE=ON` 的测试插件和 Server，设置 `renderer="qingjian"` 及 `QINGJIAN_GNOME_PROBE=1`。先验证默认面板退路和测试数据隔离，再逐项记录实际前端/坐标/窗口身份。当前仅 dbus 相对坐标通路具有实验凭证；IBus、分发沙箱和其余应用仍需验证。
4. 实测100%/150%/约167%、原生浏览器/GTK/Qt真实候选定位点击失焦，保存固定文本截图、误差和故障默认恢复证据。IBus消息缺少时间戳，重新登录也不会解决其窗口凭证；只有方案阶段0完整门槛通过才继续正式异步架构与发布工作。
5. 测试结束在该用户下 `gnome-extensions disable qingjian-probe@qingjian.local`，恢复 `renderer="fcitx"` 并重启该用户的Fcitx/Server；只移除测试扩展目录，不删除用户词库或配置。普通安装默认仍不启用此实验。
