# GNOME 阶段 0 实施记录

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
| 1–2 正式所有权及GNOME后端 | 未实现完整生产共享桥接、连续所有权和正式协议；实验仍逐帧Withdraw/准备，Actor整位图接收指针，正常安装不启用 |
| 3 尺寸/性能 | 独立配置、portal文字倍率、原生输出raster、跨屏重绘、XCB连续更新完成；XWayland一致性和受控完整性能未通过 |
| 4 应用与故障 | 隔离GTK/Qt及裸Firefox部分组件通过，微信/Chrome/VS Code/终端、分发沙箱和锁屏/热插拔等正式矩阵缺失 |
| 5 默认启用/发行 | 未改默认、未提供宣称完整支持的发行包；等待前置门槛 |
