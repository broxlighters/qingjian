# Linux 自绘候选面板支持矩阵

记录日期：2026-09-16。本次完成 C ABI、显示协议、控制器和受构建开关保护的 XCB 原型；阶段 3 的安装、卸载、资源校验和回退验收已完成，**尚无声明支持的自绘桌面路径**。自动化检查不能代替真实应用窗口测试。当前没有已验收的自绘路径，因此 `auto` 始终回退；普通安装的 `qingjian` 也回退。

## 应用与承载

| 场景 | 普通构建行为 | 自绘验收状态 |
| --- | --- | --- |
| GTK 编辑器（原生 Wayland） | 默认回退 | 未验证；无可用 Wayland popup 后端 |
| Qt 应用（原生 Wayland） | 默认回退 | 未验证；无可用 Wayland popup 后端 |
| 浏览器（原生 Wayland） | 默认回退 | 未验证；未声明自绘支持 |
| Electron 编辑器（原生 Wayland） | 默认回退 | 未验证；未声明自绘支持 |
| GNOME Terminal / 其他终端（原生 Wayland） | 默认回退 | 未验证；未声明自绘支持 |
| GTK4 GNOME Text Editor（强制 XWayland） | 默认回退 | 已启动独立测试进程并确认 `qingjian` addon 加载；未取得可重复的青简 InputContext 会话，不标自绘通过 |
| X11 / XWayland 应用 | 默认回退 | 仅 `QINGJIAN_RENDER_FFI=ON`, `QINGJIAN_EXPERIMENTAL_X11=ON` 且 `renderer="qingjian"` 的实验构建尝试自绘；窗口底层冒烟通过，真实应用移动/缩放/焦点/点击待实测 |
| GNOME Shell 搜索框 | 默认回退 | 未验证；尚未实现 Shell 展示适配 |

本环境记录为 Ubuntu 26.04、GNOME Shell 50.1、Fcitx5 5.1.19、GTK 4.22.4、XWayland 24.1.10、Firefox 155.0.1、Chromium 151.0.7922.108、Electron（`/snap/bin/code` 可用）、Ptyxis 50.1；会话为 `XDG_SESSION_TYPE=wayland`，同时有 `DISPLAY=:0`、`WAYLAND_DISPLAY=wayland-0`。阶段 3 用临时 XDG 目录和独立 D-Bus 会话启动 GTK4 GNOME Text Editor，强制 `GDK_BACKEND=x11`，确认实验插件加载且未修改现有 Fcitx；服务没有收到该进程的输入上下文握手，因此该记录只证明安装和插件加载，不证明应用自绘。

另通过显式的 XCB 测试连接在本机 XWayland 创建固定色块窗口：验证创建时隐藏、四角定位、RGBA→BGRA 实际像素、不改变焦点、隐藏和销毁，并向该测试窗口发送合成鼠标事件，验证队列读尽与换帧后事件丢弃。测试只操作自己创建的窗口，不切换或修改用户输入法。该结果是窗口后端冒烟，不能替代上述应用验收。

## 实验构建边界

仅 `QINGJIAN_RENDER_FFI=ON`、`QINGJIAN_EXPERIMENTAL_X11=ON` 加 `renderer="qingjian"` 同时满足时探测 XCB。安装脚本通过 `--experimental-x11` 显式启用；默认关闭并明确覆盖 CMake 缓存。要求上下文有焦点、明确的 `x11:` display、非空光标坐标、普通输入能力，以及 X server 上存在合成器与 ARGB8888 visual。不满足时保留默认面板。

原型连接上下文报告的 X display，不使用全局默认 display 代替。窗口创建时不 map，不请求激活；上传前验证像素格式，将预乘 RGBA 转 BGRA，并按 X 请求最大长度分块。背景 pixmap 由 X server 保持以应对 expose。坐标直接使用 Fcitx 的物理光标矩形，不再乘一次 scale；字形按客户端 scale 整形。位置按 RandR 1.5 的单个物理屏幕与当前 `_NET_WORKAREA` 的交集裁限，底部空间不足时移到光标上方；屏幕间隙使用最近屏幕。收到 RandR/工作区变化先隐藏旧窗口、回退默认面板，下一帧刷新几何。缺少 RandR 或有效区域时直接回退。

**仍未具备生产支持条件：** 多屏保留区域、热插拔、分数缩放、全屏覆盖、非激活行为和双窗抑制均待实测；EWMH 全桌面工作区也可能比实际各屏可用区域保守。`theme="system"` 已通过 [Settings portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Settings.html) 异步初始化并订阅主题变化，明确 `light` / `dark` 不受系统通知影响；portal 不可用或无偏好时采用浅色。已做独立 D-Bus 会话回归，真实桌面切换仍待验收。

## 生命周期与曝光

窗口、结果和 fd 监听随 InputContext 销毁。连接代次 + UUID + Server revision 标识帧；本地 revision 另用于失焦、隐私变化和旧候选回调。X 请求序号过滤换帧前排队的鼠标事件。默认候选列表回调用上下文弱引用，销毁后的翻页、向另一上下文重放的点选均丢弃。reset、deactivate、隐私边界、断线（包括空闲 socket 断开）均隐藏窗口并清理暂存帧。

每次接管前清空原 InputPanel 并 flush 默认 UI；恢复候选和 preedit 后再安装回调，由回调贴图。回退只解除回调、隐藏窗口并更新已有默认面板，不发送额外 Commit/Key。虚拟键盘优先级仍由 Fcitx 调度，未调用自绘回调时不回报自绘曝光。

`LinuxHello` v1 只在旧 `OpenSession` 应答包含 `linux_ui.version=1` 后发出；旧客户端/旧 Server 仍使用原通路，Windows 协议 v4 不变。所有新客户端都协商显示配置，与是否具备自绘能力无关，三种拼音位置在默认面板也生效。新客户端的曝光改由 `DisplayAcknowledged` 报告原候选槽位/义项索引；Server 校验身份、焦点、私密状态和整份索引后才覆盖当前曝光集合。自绘回报完整可见义项，默认 UI 只估计第一条义项，旧客户端仍沿用整页估计。同一帧的重绘、缩放及回退允许替换曝光集合，上屏才计数；不能只按 identity 去重。私密输入不缓存供重绘的 Frame，也不回报曝光。

## 复现与验收待办

构建命令见 [Linux 工程记录](linux-fcitx5.md)。自动化覆盖新旧握手、三种 preedit 配置、默认回退、候选/翻页命中、C ABI 无效参数与释放、真实 socket 身份重映射、旧帧/私密曝光拒绝、失焦恰好一次上屏及卸载保留数据。控制器回归经真实 Rust 命中区域与 Fcitx fd 事件循环验证阴影、空槽、原槽位选词、前后页码、滚轮、无效帧及同步隐藏；XCB 先读尽内部事件队列，忽略无效点击后继续处理，选词或翻页后丢弃同批剩余事件，避免事件滞留或误选新页。

真实桌面仍须按方案逐项记录：应用版本、frontend、ClientSideInputPanel、display、cursorRect、scaleFactor、窗口移动/缩放、四边、多屏、100/125/150/200%、全屏、点击/滚轮、输入法切换、失焦、私密输入与服务重启。只用固定测试语句截图；在真实应用路径通过前，不扩大 `auto` 的能力表。

2026-09-16 当前工作区验证记录：

| 检查 | 结果 |
| --- | --- |
| Rust workspace（排除 macOS 壳）、fmt、clippy | 基线记录中的测试及静态检查通过；阶段 3 未改 Rust 业务逻辑 |
| 渲染器 macOS ARM64 / Windows GNU 编译检查 | 均通过；不是目标系统运行验收 |
| Fcitx 普通构建 | 24 项 CTest 通过 |
| Fcitx render-ffi + 实验 XCB | 26 项 CTest 通过，含真实 X server 冒烟 |
| 默认安装（链接 FFI、关闭 XCB） | 临时前缀安装通过；全新 XDG 目录生成配置、Linux v1 握手、`nihao` 上屏“你好”通过 |
| 卸载与资源 | SHA256SUMS 校验通过，卸载保留用户配置和数据标记 |
| GTK4 XWayland 插件探测 | 独立 D-Bus/XDG 目录启动并加载 `qingjian` addon；未完成 InputContext 握手，标记未验证 |
| 自绘性能目标（预热 p95 ≤ 5 ms / p99 ≤ 10 ms） | 失败：2× 长文本竖排为 p95 31.180 ms、p99 32.009 ms；不启用 `auto` |
| 离线视觉 | 固定字体和系统字体各 64 图生成成功，代表图已目视检查并同步到样例目录 |

普通 CI 没有桌面时，`qingjian-xcb` 显示 Skipped；需要真实 X server 时显式运行 `QINGJIAN_XCB_SMOKE_DISPLAY="$DISPLAY" ctest --test-dir target/fcitx5-ui -R qingjian-xcb --output-on-failure`。D-Bus 外观回归通过独立 `dbus-run-session` 模拟 portal，不改变本机主题。GTK4 探测使用临时 XDG 目录、`GDK_BACKEND=x11` 和 `GTK_IM_MODULE=fcitx`，结束时清理服务和窗口；不应在真实用户 Fcitx 上复用该夹具。

`QINGJIAN_UI_TIMINGS=1` 可在实验 Fcitx 进程日志记录 UI total/layout/raster/upload 纳秒，不包含文本和 IPC 等待。离线基线命令为 `cargo run -p qingjian-render --example timing --release --locked`，统计 100 次预热样本 p50/p95/p99；每个场景 first 可能复用先前场景的字形缓存。只有进程首次渲染是完全冷启动。离线结果不包含窗口贴图，也不能声称已达到端到端 p95 ≤ 5 ms / p99 ≤ 10 ms。

本次系统字体离线测量覆盖 16 种场景，原始数据见 [linux-ui-timing.csv](linux-ui-timing.csv)：字体初始化 1.537 ms；预热 p95 最大 31.180 ms、p99 最大 32.009 ms（2× 长文本竖排），超过方案设定的 p95 ≤ 5 ms / p99 ≤ 10 ms 目标。场景首次绘制最慢 102.996 ms，说明冷字形和长文本首次整形仍有明显成本；当前不能宣称达到性能目标，也没有端到端贴图耗时记录。
