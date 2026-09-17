# GNOME 阶段 0 实施记录

## 2026-09-17 正式组件实施补充

本轮已新增独立生产 `qingjian@qingjian.local` / `org.qingjian.Panel1`：模块化 Shell 服务、共享异步桥接、完整64位身份、准备/显示两阶段、连续所有权、最新帧背压、250 ms绘制截止、500 ms租约及独立释放/曝光回执。FFI 普通构建链接生产桥接，`qingjian` 请求已证明的 dbus frontend；不需要 `QINGJIAN_GNOME_PROBE`。`auto` 仍不放行，完整应用和物理会话门槛没有因实现组件而取消。

独立复测使用生产扩展及本轮 freshly built addon，在 GNOME 50.1 `--no-x11` 私有会话通过 GTK 单窗鼠标、同 PID 双窗 A→B→A、167%→100%→167%双输出和 bridge 退出撤窗；退出样本约56 ms隐藏。原始证据索引在工作目录 `target/gnome-implementation/`；均为隔离组件结果，不能替代物理登录、发行沙箱、Chrome、微信、VS Code、终端或性能全矩阵。生产协议见 [协议文档](../design/linux-gnome-panel-protocol.md)。

尺寸根因调查新证据：同一时刻 Mutter eDP 逻辑1728×1080、倍率1.6666666；HDMI逻辑1920×1080、倍率1。RandR分别报告3456×2160和3840×2160，两屏root:stage均为2:1。Fcitx `InputContext::setCursorRect` 无scale重载强制1.0，XIM/IBus与旧D-Bus方法都使用该重载，因此客户端scale不是可信的root raster。新增全输出几何联合解析，在全部输出位置/大小能匹配时采用唯一比率，否则 `scale_unresolved`。还需记录反馈应用的实际上下文scale与同屏对照截图，才能勾选尺寸专项完成。

服务默认绑定图形目标、后台模式沿用同名unit、迁移/停用保留、只读诊断、安装暂存rename和回滚已实现，并有私有socket/文件/假systemctl回归。真实独立user manager与物理登出重登/重启仍须完成，不把夹具通过记为会话验收。旧原装Kimpanel恢复定位缺陷仍未作为可分发依赖解决，完整发布继续受阻。

第二轮生产修复补充：新增有效交互区域 FFI 与子 Actor 事件，非左键/空槽/页边界无动作不关闭交互；生产 GTK 真点击恰好上屏一次并撤窗（`target/r2/shell-mv11vxvz`）。首次 Hello 协商进入有期限 Preparing，正常空帧 Idle/Hide；固定错误原因传到诊断。XCB 的 RandR/工作区改变通知 Controller 作废旧命中并自动重绘当前有效帧，不需要下一键。安装新增不可变逐代快照和会话模式恢复、旧 Debian/扩展停用迁移以及驻留 deleted inode 诊断。

本轮 Rust fmt/clippy 通过，workspace 排除 macOS 的测试443通过/1原有忽略；生产39项CTest中38通过、XCB冒烟1项按环境跳过后在独立 Xvfb+xcompmgr补测通过；其中工作区自动重绘、旧点击丢弃、首次握手、空帧及真实 D-Bus `buffer_invalid` 回退保因均有回归。生产/探针Node、管理故障注入、卸载保留数据均通过。Debian样例包和sha256位于 `target/gnome-implementation/deb/`，已核对正式扩展/管理入口/autostart/unit及资源校验；样例包不是完整产品数据发布包。原始日志为该目录上级的 `round2-*`。

父任务最终源码组件复验 `target/qjf-r3final/report.json` 为15/15通过：GTK五档倍率、Qt、GNOME Text Editor50.1、Firefox155.0.1裸程序、同PID多窗、双输出、系统文字1.25/UI150、pause/exit/overview。12个正常场景无reason回退、无相关GLib错误，固定文本截图已保存。Firefox裸程序不代表Snap发行沙箱；这些结果不替代物理会话、尺寸定位精度与性能验收。完整包抽出组件的同矩阵复验 `target/qjf-r3package/report.json` 也为15/15通过，12个正常场景无回退，3个故障场景按预期撤窗；第四轮管理修复后的包内插件、Server和扩展与该轮组件逐字节一致。尺寸摘要与截图索引在 `target/gnome-implementation/parent-r3-desktop-summary.json`；五档Actor逻辑宽约221/220.8/220.667/220.8/220.5、高180，文字1.25为253×198，UI150为331×270，仅为隔离组件尺寸证据。

第四轮管理修复仅分开旧服务账户快照与扩展安装/停用证据：首次随Debian引入的新扩展正常登记，Shell明确停用或源码安装的既有扩展停用仍保留。新增回归覆盖两条边界；UI源码保持第三轮冻结。

以下为之前两轮阶段0实验的历史记录；其中“正式实现未提供”的状态已被上文更新，其余真实应用与发布门槛继续有效。

第三轮审查修复：第二轮最终双输出失败的根因分为输出刷新重新 Hello 与 Bind 的竞争，以及 Fcitx `accuracy=0` 把100 ms续约合并至约250 ms。现在输出快照独立传输，不清连接 epoch；Hello 使用独立 owner 代次；D-Bus 错误只接受固定原因码；定时器明确1 ms精度，并在实际 Painted 后续展仍有效的窗口凭证。生产GTK双向跨屏两次、Qt双向跨屏一次均无回退且最终只上屏一次“你好”，证据为 `target/r3fix/shell-t8wrcayu`、`target/r3again/shell-egkbspct`、`target/r3qt/shell-zdahzuk8`；pause故障仍能撤窗（`target/r3pause/shell-732y88le`）。这些固定等待夹具不提供重绘性能结论。

第三轮还将 Debian 全机升级布尔标记改成首次迁移时的账户身份快照：旧停用用户与升级后新账户分别测试通过，后续升级不扩大集合，postinst 不访问用户家目录或总线。XCB正常隐藏、失焦和空帧的Idle状态新增诊断回归。39项CTest中38通过、XCB环境skip；Rust未改动，沿用第二轮443通过/1忽略证据，fmt及生产/探针Node、管理、卸载和最小构建重新通过。父任务的editor/terminal夹具新增项仍须分别记录：editor有组件证据，terminal因私有user manager缺失尚未进入输入测试，不能记为已支持。

2026-09-17，第二轮。已完成 **dbus 输入模块的窗口身份证明、独立尺寸链路和隔离双输出重绘**；方案阶段 0 尚未完整验收。默认仍为 `fcitx`，`auto` 没有放行路径；正常安装不包含实验 Shell 后端。第一轮 program/wmclass 单窗口实验已被下述 v2 凭证替换，旧结果仅保留作历史证据。

## 环境与证据范围

实机 Ubuntu 26.04，GNOME Shell 50.1（Ubuntu `50.1-0ubuntu1.1`），Fcitx5 `5.1.19-1`，GJS `1.88.0`，GTK4 `4.22.4`，Qt `6.10.2`。实际内屏 2880×1800 / 120 Hz / 约167%，外屏 1920×1080 / 60 Hz / 100%；文字倍率1.0。没有修改用户实际显示器、输入法或 Shell 设置。[环境采集](linux-gnome-evidence/environment.json)。

本轮真实软件链运行于独立 D-Bus、HOME/XDG 和 GNOME 50.1 headless Wayland 会话；XCB 后端关闭，Shell `--no-x11`，GTK/Qt 使用系统 Fcitx 输入模块。通过 Mutter `ApplyMonitorsConfig` 配置临时输出100%/150%/约167%，不是注入虚假倍率。文字倍率使用显式启动的系统 `xdg-desktop-portal-gnome` 与 `xdg-desktop-portal`。输入固定 `nihao`，严格检查恰好上屏、同一焦点窗口、选后隐藏。

这些是输入、Shell 和渲染组件联测，**不等于物理登录会话、目标发行沙箱、定位≤2逻辑像素或性能矩阵验收**。`Painted` 表示 mapped Actor 的 stage `after-paint`，不表示显示器扫描输出。真实登录会话尚未发现新扩展 UUID；重新登录只能解除装载限制，不能解除 IBus 身份等架构缺口。

## dbus 窗口身份闭环

Fcitx 公开的 `ObjectVTableBase::currentMessage()` 能在真实 dbusfrontend 的同步 KeyEvent 调用栈中提供来源。插件验证已注册对象、接口、路径、签名及 `ProcessKeyEvent` / `ProcessKeyEventBatch`，复制唯一 sender、IC UUID、路径、IC 焦点代次和按键时间/键码；不读取或 rewind 消息正文，不保留消息/IC 裸指针。框架已验证发送者，只在有效焦点的真实事件中捕获。组件测试实际加载系统 dbus/dbusfrontend，覆盖主服务、portal、伪造 sender、错接口、回调外读取和32位时间绕回。

插件在实际 frontend 所在总线查询 daemon ID，与 Shell session 总线比较；Shell 再确认桥接发送者与 `org.fcitx.Fcitx5` 的真实 PID 一致。Shell 在具体 Window 的 `notify::user-time` 同步回调中取得当前真实 `Clutter.KEY_PRESS`，精确匹配无符号32位时间和键码，签发绑定窗口对象、Shell焦点代次、IC UUID/代次、来源与桥接连接的短期凭证。sender PID 仅作同进程辅证，不能单独证明窗口。程序名和 wmclass 不参与授权。

按键证据最多保留500ms/32条且仅在内存；凭证不携带键元数据。歧义拒绝、单次签发、消费墓碑防重放；同来源在途 Bind 可合并，跨 IC/代次/owner 不可复用。Reset、实际 FocusIn/Out、能力变化均增加 IC 代次，Shell失焦、应用点击、Overview、锁屏/会话变化、销毁、到期撤销。Mutter 对同一键的 `user-time=0` 校正通知不算新键，也不撤销已证明事件，避免 Qt 第二窗口最后一个字母丢候选。

Qt 的 program=`python3.14`、wmclass=`qingjian-qt-test` 仍实际不同，但已能正常自绘。GTK/Qt 双输入框与同 PID 两个 mapped 窗口 A→B→A 均有独立复测；同窗口不同 IC 和复用 IC 的 epoch 均受到检查。

**范围仍只覆盖 dbus 输入模块。** Fcitx5.1.19 IBus `ProcessKeyEvent` 的 `uuu` 消息没有时间戳，构造的 KeyEvent time=0；不能复用这条时间/键码窗口证明。GNOME 内建 text-input、IBus 和其它未知前端继续默认回退，需要新的可验证来源设计。不能把 dbus 的结论推广为全部 GNOME 输入前端。

## 几何与尺寸

Shell 把相对 cursor / clientScale 加到目标窗口 buffer 原点，按窗口当前 monitor 的 raster 栅格化并用工作区裁限。clientScale 与 raster 分开：GTK150%时 clientScale=2、raster=1.5；Qt则两者约1.5。这避免把工具包的整数倍率当作输出倍率。

输出、工作区或倍率改变，先撤旧纹理、按下状态及在途 after-paint，再发 `GeometryChanged(oldToken)`；同窗口有效焦点凭证保留。插件使用弱引用上下文的最新 cursor/scale 和新 submission 重新 Bind/render/upload，旧 Painted/Pointer/重复几何信号因 token/serial 失效。订阅 Window `notify::main-monitor`、位置/尺寸，Display `window-entered-monitor`、`workareas-changed` 及 layoutManager `monitors-changed`。连续准备中的几何变化共用250ms截止，Bind/重试不延长焦点凭证；只有有效显示续约才延长500ms租约。

| 隔离场景 | 观察结果 |
| --- | --- |
| GTK / Qt 100%、150%、约167% | 鼠标精确“你好”，逻辑宽约221、高180；栅格随输出变化 |
| GTK / Qt 候选显示中167%→100%→167% | 不增加按键，宽220.8→221→220.8、高180，同一焦点窗口，最终鼠标选词通过 |
| 系统文字倍率1.25，跟随开启/关闭 | Actor253×198 / 221×180，真实portal返回1.25 |
| 显示中系统文字倍率1→1.25 | reviewer独立记录221×180→253×198，重新定位后鼠标精确“你好” |
| 用户UI125% / 150% | Actor276×225 / 331×270，真实Server配置输入 |

`[linux_ui] ui_scale_percent=75..200`（默认100）、`follow_system_text_scale=true` 贯穿 Config → Server 可选尺寸协商 → Fcitx → sized render-ffi。布局/留白/装饰只乘UI倍率，文字另乘系统倍率，位图独立乘raster。旧ABI与其它平台默认尺寸不变。Appearance在所有FFI构建初始化，包括关闭X11的Wayland实验。portal不可用时取1并保留来源未知状态；用户配置目前需重启Server/Fcitx。

XCB仍使用现有 `InputContext::scaleFactor()`，诊断标记 `context-unverified`，不以Xft DPI盲目相乘。原生和XWayland的同屏尺寸一致性、物理跨屏定位误差、动态UI配置热重载尚未验收。跨屏夹具的约400ms输出含固定稳定等待，**不是重绘延迟样本**。

## 默认面板恢复与上游缺陷

能力变化 watcher 现在对仍活动的青简会话重建默认帧，解决 Kimpanel 重载两次 capability 改变后永久空窗。只清自己持有的 callback，不覆盖其它输入法回调；虚拟键盘与隐私仍优先。扩展禁用后，原装Fcitx5.1.19 + GNOME Kimpanel的默认候选约101–113ms恢复可见且能鼠标精确选词；重新启用扩展后，新输入重新自绘，两次恰好“你好你好”。这是有真实默认UI的结果，区别于第一轮 `-u none` 仅验证撤窗。

原装Kimpanel还有**恢复位置落到屏幕左上角**的已证实缺陷：`resume()`只监听未来cursor/focus事件，异步Introspect完成后没有发布现有光标。客户端上报相同矩形会被框架去重，默认第一候选成为 `[13,71,...]`。没有在青简壳里捏造FocusIn或用固定延迟移动默认面板。

独立 [Kimpanel上游实验补丁](../../apps/linux/upstream/fcitx5/fcitx5-5.1.19-kimpanel-resume.patch) 在Introspect结束后调用既有cursor路径刷新当前focused IC；协商期间不把relative坐标误作绝对坐标，suspend取消未完成查询。显式隔离加载该addon后第一候选为 `[375,397,...]`，同场自绘原点 `[368,465]`；coder/reviewer分别约110.8/142.8ms恢复，默认点击及重启自绘点击均精确通过。**补丁未安装到系统、未纳入产品依赖、未提交上游；原装包的定位问题仍是外部限制。** 构建/复现见上游README。

## 历史位图与 XCB 调查

第一轮独立色块证明密封memfd → GJS异步读取 → `St.ImageContent` → mapped/after-paint → Hide，[结果](linux-gnome-evidence/bitmap-result.json)。第一轮GTK/Firefox program匹配实验、Qt仅键盘基线以及500ms租约故障样本都保留在证据目录，不能当作当前v2身份验收。Firefox裸Snap目录二进制的成功也不能替代Snap沙箱发行包；Google Chrome未覆盖。

XCB保持checked错误检查，同一有效display连续更新避免每帧unmap/map。单场景100预热/1000样本调查：hide p95/p99=689.173/774.670ms，update=1.522/2.038ms；后续默认/debug build=7.045/7.601ms，统一release build=1.411/1.594ms。逐次数据与汇总在[证据目录](linux-gnome-evidence/README.md)。这些非受控桌面单场景数据不能替代完整性能矩阵；失效后实际默认鼠标选词有独立Xvfb记录。

## 检查、审查与剩余阶段

适用Linux workspace的fmt、clippy（`-D warnings`）、tests在第一轮通过：443通过、0失败、1项原有忽略；第二轮未改Rust。第二轮X11+FFI 34项CTest执行通过（其中两项先跳过，再用独立Xvfb+xcompmgr实际补测），最小FFI/X11/probe全OFF构建通过。实验FFI/noX11/probe构建38项通过；包含真实dbus来源、其它IME callback、Appearance、Prepare中几何变化、晚到信号及连续几何更新250ms截止。Node运行真实扩展方法与凭证逻辑回归通过。

并发构建/桌面实验时另有一轮38项中4项受到严重调度延迟而触发paint_timeout或墙钟断言；保留失败摘要与后续定向/完整复测，不放宽生产250ms期限；传输单测随后在计时前预热固定字形，7项再通过。墙钟断言仍受系统调度影响，不作性能保证。真实Shell也见过冷首帧paint_timeout后下一次输入恢复，不能声称无闪烁、无首次回退或性能达标。

本轮按coder实现→reviewer审查→coder修复→复审推进。身份/代次、重复user-time通知、外部callback、无X11的Appearance、默认恢复空窗、跨屏误撤焦点proof已修复并经独立复测。审查结论仅针对已实现实验代码；最终状态见父任务交付与证据快照，不代表整份方案通过。

| 方案阶段 | 当前状态 |
| --- | --- |
| 0 接口与真实通路 | dbus身份、GTK/Qt/Firefox固定输入组件、三档倍率和双输出具备证据；物理登录、IBus/发行沙箱/完整目标矩阵未通过 |
| 1–2 正式所有权及GNOME后端 | 生产共享桥接、连续所有权、正式协议及按有效交互区接收指针已实现；隔离生产 GTK 单/多窗、双输出、断线与点击通过，物理登录及完整应用矩阵未通过 |
| 3 尺寸/性能 | 独立配置、portal文字倍率、原生输出raster、跨屏重绘、XCB连续更新和XWayland全输出几何解析完成；同屏实物对照与受控完整性能未通过 |
| 4 应用与故障 | 隔离GTK/Qt及裸Firefox部分组件通过，微信/Chrome/VS Code/终端、分发沙箱和锁屏/热插拔等正式矩阵缺失 |
| 5–6 服务/安装/发行 | 服务模式、诊断、事务安装、Debian样例包、升级停用偏好及逐代回滚已实现并通过隔离回归；未改默认，等待真实登录/重启、完整包升级及应用矩阵门槛 |
