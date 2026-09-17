# Linux 自绘候选面板支持矩阵

2026-09-17 生产组件更新：FFI 普通构建新增 `Panel1` 共享异步桥接，完整 GNOME 包新增 `qingjian@qingjian.local`。显式 `renderer="qingjian"` 时只放行具有同源按键窗口证明的 dbus frontend；IBus和未知通路仍回退。生产 GTK 在隔离GNOME无X11会话通过单窗鼠标、同PID双窗、167%/100%跨输出与进程退出撤窗。XWayland新增全输出root→Shell几何倍率解析，无法联合校验时回退。默认仍为 `fcitx`，`auto` 仍关闭。

这不是完整正式支持声明：目标应用的发行包矩阵、物理会话、同屏尺寸误差、受控可见延迟与Kimpanel恢复定位依赖均未完成。下面“无生产GNOME后端”等旧描述保留为历史构建记录；本轮代码与门禁以 [最新阶段记录](linux-gnome-stage0.md) 和 [协议](../design/linux-gnome-panel-protocol.md) 为准。

记录日期：2026-09-17。已完成 C ABI、显示协议、后端选择器、可选 X11 承载与 Wayland 独立探针；前序 Linux 安装阶段的安装、卸载、资源校验和回退验收已完成，**尚无声明支持的自绘桌面路径**。自动化检查不能代替真实应用窗口测试。当前没有已验收的自绘路径，因此 `auto` 始终回退；`qingjian` 在可用 X11/XWayland 上请求自绘，原生 Wayland 继续回退。

## 应用与承载

GNOME专项新增**不随正常构建启用**的v2 Shell实验：真实dbus消息来源和Shell窗口按键证明已替换program/wmclass匹配；隔离GTK/Qt单框、同窗双框及同进程多窗A→B→A、100%/150%/约167%、167%→100%→167%重绘、portal文字倍率与UI倍率均有鼠标闭环。默认Kimpanel恢复和重新接管可用，但系统原包恢复定位错误，只有独立上游实验补丁修复定位。IBus缺少可复用时间戳，正式连续所有权、物理登录/发行沙箱/应用矩阵仍未通过。普通构建与下表行为不变，证据见[阶段0实施记录](linux-gnome-stage0.md)。

| 场景 | 普通构建行为 | 自绘验收状态 |
| --- | --- | --- |
| GTK 编辑器（原生 Wayland） | 默认回退 | 未验证；无可用 Wayland popup 后端 |
| Qt 应用（原生 Wayland） | 默认回退 | 未验证；无可用 Wayland popup 后端 |
| 浏览器（原生 Wayland） | 默认回退 | 未验证；未声明自绘支持 |
| Electron 编辑器（原生 Wayland） | 默认回退 | 未验证；未声明自绘支持 |
| GNOME Terminal / 其他终端（原生 Wayland） | 默认回退 | 未验证；未声明自绘支持 |
| GTK4 GNOME Text Editor（强制 XWayland） | 默认回退 | 已启动独立测试进程并确认 `qingjian` addon 加载；未取得可重复的青简 InputContext 会话，不标自绘通过 |
| GTK4 测试输入框（隔离 Xvfb + xcompmgr） | 显式请求自绘 | 输入、鼠标选词、不抢焦点和合成器退出回退通过；仅 800×600、100% 缩放，不等于 GNOME/KDE 应用验收 |
| X11 / XWayland 应用 | 默认回退 | 仅 `QINGJIAN_RENDER_FFI=ON`, `QINGJIAN_X11_BACKEND=ON` 且 `renderer="qingjian"` 的构建尝试自绘；窗口底层冒烟通过，真实应用移动/缩放/焦点/点击待实测 |
| GNOME Shell 搜索框 | 默认回退 | 未验证；尚未实现 Shell 展示适配 |

本环境记录为 Ubuntu 26.04、GNOME Shell 50.1、Fcitx5 5.1.19、GTK 4.22.4、XWayland 24.1.10、Firefox 155.0.1、Chromium 151.0.7922.108、Electron（`/snap/bin/code` 可用）、Ptyxis 50.1；会话为 `XDG_SESSION_TYPE=wayland`，同时有 `DISPLAY=:0`、`WAYLAND_DISPLAY=wayland-0`。阶段 3 用临时 XDG 目录和独立 D-Bus 会话启动 GTK4 GNOME Text Editor，强制 `GDK_BACKEND=x11`，确认实验插件加载且未修改现有 Fcitx；服务没有收到该进程的输入上下文握手，因此该记录只证明安装和插件加载，不证明应用自绘。

另通过显式的 XCB 测试连接在本机 XWayland 创建固定色块窗口：验证创建时隐藏、四角定位、RGBA→BGRA 实际像素、不改变焦点、隐藏和销毁，并向该测试窗口发送合成鼠标事件，验证队列读尽与换帧后事件丢弃。测试只操作自己创建的窗口，不切换或修改用户输入法。该结果是窗口后端冒烟，不能替代上述应用验收。

后续使用 GTK4 `Entry` 在独立 Xvfb、D-Bus 与全新 XDG 目录中完成真实 Fcitx dbusfrontend 输入链路：固定样例 `nihao` 通过键盘或点击候选恰好上屏“你好”，点击前后键盘焦点一致；终止夹具自己的 xcompmgr 后，自绘窗口撤下，默认面板仍可完成上屏；无合成器启动也能使用默认面板。已目视检查真实候选截图中的拼音、中文、释义和 emoji。该结果补足隔离 X11 的输入链路证据，没有覆盖真实桌面窗口移动、多屏、分数缩放或原生 Wayland。

可复现夹具位于 `apps/linux/fcitx5/tests/desktop/`，依赖 `python3-gi gir1.2-gtk-4.0 fcitx5-frontend-gtk4 xvfb xcompmgr xdotool x11-utils dbus`，另外需要已构建的 Server 和 FFI + X11 addon。四个独立用例为 `keyboard`、`mouse`、`compositor-loss`、`no-compositor`：

```bash
/usr/bin/python3 apps/linux/fcitx5/tests/desktop/run.py mouse \
  --server target/release/qingjian-linux-server \
  --addon target/dual-ui/x11/qingjian.so
```

夹具只启动并终止自己的进程；使用 Xvfb `-displayfd` 分配 display，测试时不会操作用户桌面的窗口或 Fcitx。`--xvfb`、`--compositor`、`--xdotool` 可指定隔离提取的工具，`--xwd` 可保留候选截图。日志、结果与配置保存在 `target/dual-ui/gtk-<case>-*/`。固定 100% 缩放下的点击坐标只服务于样例夹具；未列入无桌面 CI，也不作为 `auto` 放行依据。

本轮补充：GTK4 隔离夹具现于启动私有 D-Bus 前固定 HOME/XDG/DISPLAY，使用无 servicedir、无 systemd activation 的 bus 配置，验证 `ListActivatableNames` 仅有 D-Bus 自身；结束后扫描 fixture token 进程确认无泄漏。`no-compositor` 与 `compositor-loss` 必须看到可见的 `Fcitx5 Input Window` 并点击默认面板选词；禁用 Classic UI 的反例会失败。GDK_SCALE/GDK_DPI_SCALE 在夹具内固定为 1，Entry 实际 scale 写入结果。固定 Xvfb 四用例及两个禁用默认 UI 反例均按此规则运行通过。

`regression.py` 接受与 `run.py` 相同的工具/Server/addon 参数（不带 case），一次运行四个正例和两个 `--without-classicui` 反例，并注入外部 2× 缩放参数验证隔离。最终记录在 `target/dual-ui/gtk-fixture-fixed-final.log`；旧 `gtk-fixture.log` 不作为隔离与默认面板恢复的最终证据。

## 构建与能力边界

仅 `QINGJIAN_RENDER_FFI=ON`、`QINGJIAN_X11_BACKEND=ON` 加 `renderer="qingjian"` 同时满足时探测 XCB。安装脚本默认包含 X11 后端，最小安装用 `--disable-x11` 显式关闭；开启时要求 XCB 开发包齐全。要求上下文有焦点、明确的 `x11:` display、非空光标坐标、普通输入能力，以及 X server 上存在合成器与 ARGB8888 visual。不满足时保留默认面板。

后端连接上下文报告的 X display，不使用全局默认 display 代替。窗口创建时不 map，不请求激活；上传前验证像素格式，将预乘 RGBA 转 BGRA，并按 X 请求最大长度分块。背景 pixmap 由 X server 保持以应对 expose。坐标直接使用 Fcitx 的物理光标矩形，不再乘一次 scale；字形按客户端 scale 整形。位置按 RandR 1.5 的单个物理屏幕与当前 `_NET_WORKAREA` 的交集裁限，底部空间不足时移到光标上方；屏幕间隙使用最近屏幕。收到 RandR/工作区变化先隐藏旧窗口、回退默认面板，下一帧刷新几何。缺少 RandR 或有效区域时直接回退。

**仍未具备生产支持条件：** 多屏保留区域、热插拔、分数缩放、全屏覆盖、非激活行为和双窗抑制均待实测；EWMH 全桌面工作区也可能比实际各屏可用区域保守。`theme="system"` 已通过 [Settings portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Settings.html) 异步初始化并订阅主题变化，明确 `light` / `dark` 不受系统通知影响；portal 不可用或无偏好时采用浅色。已做独立 D-Bus 会话回归，真实桌面切换仍待验收。

Wayland 生产后端尚未接入：本机独立连接没有 input-panel v1/v2 全局，v2 的 Fcitx 包装接口也不是已安装公共 ABI。阶段 0 证据、色块探针与独立 API 扩展任务见 [Wayland 承载核验](linux-wayland-api.md)。没有 KDE/其他 compositor 及 GNOME 扩展验收记录，不能宣称双模式已完成。

## 生命周期与曝光

窗口、结果和 fd 监听随 InputContext 销毁。连接代次 + UUID + Server revision 标识帧；本地 revision 另用于失焦、隐私变化和旧候选回调。X 请求序号过滤换帧前排队的鼠标事件。默认候选列表回调用上下文弱引用，销毁后的翻页、向另一上下文重放的点选均丢弃。reset、deactivate、隐私边界、断线（包括空闲 socket 断开）均隐藏窗口并清理暂存帧、文字整形缓存及生产承载。合成器 selection 查询最多每 250 ms 发起一次，提交、fd 与健康定时器非阻塞轮询 reply；owner 改变或查询超过 1 秒未响应即标记承载失效并回退，后续帧重建。新增本地 submission 代次，覆盖同一 Server identity 的重绘，事件回调执行期间持有后端寿命。

首次接管前清空原 InputPanel 并 flush 默认 UI；恢复候选和 preedit 后再安装回调，由回调贴图。同一 X11 display 的有效自绘帧直接更新，旧动作立即失效，保留映射避免每键unmap/map；准备或提交失败仍撤下旧窗口。回退只解除回调、隐藏窗口并更新已有默认面板，不发送额外 Commit/Key。虚拟键盘优先级由 Fcitx 调度，可见期间不请求自绘，准备期间出现则取消并撤窗，不回报无法确认的曝光。

`LinuxHello` v1 只在旧 `OpenSession` 应答包含 `linux_ui.version=1` 后发出；旧客户端/旧 Server 仍使用原通路，Windows 协议 v4 不变。所有新客户端都协商显示配置，与是否具备自绘能力无关，三种拼音位置在默认面板也生效。新客户端的曝光改由 `DisplayAcknowledged` 报告原候选槽位/义项索引；Server 校验身份、焦点、私密状态和整份索引后才覆盖当前曝光集合。自绘回报完整可见义项，默认 UI 只估计第一条义项，旧客户端仍沿用整页估计。同一帧的重绘、缩放及回退允许替换曝光集合，上屏才计数；不能只按 identity 去重。私密输入不缓存供重绘的 Frame，也不回报曝光。

## 2026-09-17 双模式方案实施记录

新增 display 探针、后端工厂、X11 默认构建开关与最小构建路径；旧实验缓存仅迁移一次，新开关优先。`fcitx` 仍为默认配置，`auto` 仍不放行任何未验收后端。Wayland 探针仅用于开发，既不进入安装包，也不接管现有 Fcitx。

文字整形缓存按文本/字号/行高/点字号有界复用，颜色与高亮不进入缓存键；字形覆盖率预先换算预乘颜色，按裁限后的像素行混合。已验证主题、缩放、高亮、页码、缓存清理前后像素和命中几何一致。

| 离线测量 | 场景数 | 最大预热 p95 | 最大预热 p99 | 最慢场景首帧 |
| --- | ---: | ---: | ---: | ---: |
| 优化前，同机系统字体 | 16 | 33.187 ms | 34.799 ms | 111.445 ms |
| 优化后，系统字体 | 32 | 3.330 ms | 3.459 ms | 56.931 ms |
| 优化后，固定许可字体 | 32 | 4.477 ms | 5.231 ms | 67.788 ms |

原始数据：[优化前](linux-ui-timing-before-dual.csv)、[系统字体](linux-ui-timing-system-dual.csv)、[固定字体](linux-ui-timing-fixed-dual.csv)。优化后覆盖 1/1.25/1.5/2×，短句、长文本 CJK、日文/emoji、多义项、空槽与窄屏。每场景 1 次首次渲染 + 100 次预热；字体初始化系统 1.529 ms、固定 0.939 ms；进程后续场景会复用已加载字形，所以表中首帧不是每次重启冷启动。复现：`cargo run -p qingjian-render --example timing --release --locked`，固定字体追加 `-- --fixed-fonts`。

这些数字仅表示离线渲染已低于目标，**不包含 XCB 上传、surface 可见时间或 IPC，不能据此启用 auto**。后续增加了 XCB 差分行上传，详见下一节；整行排版与位图栅格复用仍待实现。Wayland 双缓冲、KDE/其他 compositor、GNOME 扩展及真实应用矩阵仍未完成。

## 2026-09-17 X11 增量上传与实际提交测量

同尺寸帧复用一张 X server pixmap 和不超过 1600×900×4 字节的 BGRA 缓冲。格式转换时同时比较变化行；相同位图不再上传像素，高亮/页码变化只上传首尾变化行包围的区域。尺寸变化或任一提交失败后重新分配并完整上传，避免部分失败污染后续差分。测试覆盖 RGBA/BGRA、alpha、stride padding、尺寸变化、强制重传、真实像素与上传字节数。

合成器 owner 查询改为非阻塞 reply 轮询。独立 Xvfb 夹具只在自己启动的服务上模拟 selection 消失和服务器暂停，验证失效状态保持、超过 1 秒无 reply 时回退、健康查询不阻塞。此夹具不代表真实桌面合成器验收；CI 安装 `xvfb`，本机可用 `QINGJIAN_XVFB=/path/to/Xvfb` 指定隔离副本。

新增 `qingjian-xcb-timing`，与 FFI + X11 构建一起生成，必须显式传入 X display 才运行：`timeout 240 target/dual-ui/x11/qingjian-xcb-timing "$DISPLAY" > timing.csv`。只创建自己的窗口，使用固定测试语句，不连接 Server 或修改用户输入法。48 场景涵盖横竖排、长短文本、1/1.25/1.5/2×，分别测不变帧、高亮与页码变化；每个场景 1 个首次样本、100 个预热样本。字段拆分 total、layout、raster、backend、probe、convert、upload、commit 和上传字节数。`commit` 仅表示 X server 接受请求，**不表示 compositor 已呈现，且不含 IPC**。首次样本可能复用之前场景的字形，不当成进程冷启动。

本机 GNOME/XWayland 的 48 场景[原始记录](linux-ui-timing-xcb.csv)中，不变帧上传字节均为 0；最大预热 total p95 580.506 ms、p99 819.972 ms，远高于门槛。尾延迟主要在 checked commit（最大 p95 579.972 ms）；非阻塞 probe 最大 p95 0.007 ms，像素 upload 最大 p95 1.801 ms。2× 长文本中格式转换最大 p95 5.660 ms，栅格最大 p95 4.072 ms。该机器同时运行桌面应用，不能作为受控跨版本性能比较；但足以说明离线栅格通过不能证明实际提交达标。保留原始失败结果，`auto` 不放行。后续需解决同步 X checked request 的等待和实际可见时间观测，并在受控桌面重复验收。

本轮新增与既有 CTest 合计：最小构建 25 项、仅 FFI 构建 26 项、FFI + X11 构建 28 项通过，含真实 XWayland 冒烟和独立 Xvfb 失效测试。`wayland:` 空后缀按 Fcitx 默认连接识别为 Wayland，仍明确回退；`x11:` 空后缀继续拒绝。XCB 初始化失败日志分别指出连接、RandR、合成器、ARGB 或建窗阶段，只包含固定诊断文案。

前一轮缓存优化完成时的验证（X11 增量上传后的计数见上文）：

| 检查 | 结果 |
| --- | --- |
| Rust workspace（排除 macOS 壳） | 435 项测试通过；fmt、clippy（所有 target、警告视为错误）通过 |
| Fcitx 最小构建（FFI / X11 均关闭） | 24 项 CTest 通过 |
| Fcitx 仅 FFI 构建（X11 关闭） | 25 项 CTest 通过，含内存承载的控制器回归 |
| Fcitx FFI + X11 构建 | 26 项 CTest 通过，含本机 XWayland 冒烟 |
| 事件与故障回归 | 空闲合成器失效、X11→Wayland 回退、同 identity 同步重绘时丢弃旧点击通过 |
| 渲染器 macOS ARM64 / Windows GNU | 编译检查通过；不代表目标桌面运行验收 |
| CMake 开关迁移 | 旧 ON/OFF 迁移、新开关优先、迁移后重新开启均通过 |
| 样例 Debian 包 | 最终代码打包、解包、包及资源 SHA256、XCB 动态库依赖和无 staging 路径检查通过；默认 `fcitx`，未安装到系统 |

本机缺少 `libxcb-randr0-dev` 的系统安装，本轮构建通过临时提取的开发包配置 `PKG_CONFIG_PATH`；没有修改系统包或当前用户输入法。常规构建与 Debian Build-Depends 均已列明该依赖。

## 复现与验收待办

构建命令见 [Linux 工程记录](linux-fcitx5.md)。自动化覆盖新旧握手、三种 preedit 配置、默认回退、候选/翻页命中、C ABI 无效参数与释放、真实 socket 身份重映射、旧帧/私密曝光拒绝、失焦恰好一次上屏及卸载保留数据。控制器回归经真实 Rust 命中区域与 Fcitx fd 事件循环验证阴影、空槽、原槽位选词、前后页码、滚轮、无效帧及同步隐藏；XCB 先读尽内部事件队列，忽略无效点击后继续处理，选词或翻页后丢弃同批剩余事件，避免事件滞留或误选新页。

真实桌面仍须按方案逐项记录：应用版本、frontend、ClientSideInputPanel、display、cursorRect、scaleFactor、窗口移动/缩放、四边、多屏、100/125/150/200%、全屏、点击/滚轮、输入法切换、失焦、私密输入与服务重启。只用固定测试语句截图；在真实应用路径通过前，不扩大 `auto` 的能力表。

2026-09-16 基线验证记录（下表保留历史结果，新的构建边界与性能见上文）：

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

`QINGJIAN_UI_TIMINGS=1` 可在实验 Fcitx 进程日志记录 UI total/layout/raster/backend/probe/convert/upload/commit 纳秒及 upload_bytes，不包含文本和 IPC 等待；commit 不代表屏幕可见。离线基线命令为 `cargo run -p qingjian-render --example timing --release --locked`，统计 100 次预热样本 p50/p95/p99；每个场景 first 可能复用先前场景的字形缓存。只有进程首次渲染是完全冷启动。离线结果不包含窗口贴图，也不能声称已达到端到端 p95 ≤ 5 ms / p99 ≤ 10 ms。

本次系统字体离线测量覆盖 16 种场景，原始数据见 [linux-ui-timing.csv](linux-ui-timing.csv)：字体初始化 1.537 ms；预热 p95 最大 31.180 ms、p99 最大 32.009 ms（2× 长文本竖排），超过方案设定的 p95 ≤ 5 ms / p99 ≤ 10 ms 目标。场景首次绘制最慢 102.996 ms，说明冷字形和长文本首次整形仍有明显成本；当前不能宣称达到性能目标，也没有端到端贴图耗时记录。
