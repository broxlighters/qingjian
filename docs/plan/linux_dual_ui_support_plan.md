# Linux X11 / Wayland 双模式自绘候选窗开发方案

评估日期：2026-09-16。目标环境：Ubuntu 26.04、Fcitx5 5.1.19、GNOME Shell 50.1；当前会话为 Wayland，同时提供 XWayland。

本文件是完成双模式正式支持的开发计划，不表示目标功能已经完成。当前实现、测试记录与已知限制见 [Linux 自绘候选面板支持矩阵](../notes/linux-ui-support.md)。

2026-09-17 实施进度：已落地后端选择/诊断、可选 X11 构建、合成器故障回退与文字缓存优化；Wayland 独立 registry/色块探针已构建并在本机运行。阶段 0 **未通过**：本机普通 GNOME 连接没有 v1/v2 input-panel 全局，KDE/其他 compositor 尚未验收。已将第 5.1 节的 API 扩展转为固定 Fcitx 5.1.19 的本地 popup API 提案，完成公开头、同连接 surface/role、失效回调、生命周期/ASan 和消费者编译验证；提案未发送上游、未进入生产，真实 GNOME/KDE gate 仍未通过。公开连接 API 的能力、限制及独立 API 扩展任务见 [核验记录](../notes/linux-wayland-api.md)。阶段 2/3 不能标为完成；离线性能改善不能替代窗口上传和真实应用验收。

## 1. 结论与阻塞点

**可以开始开发，没有阻塞开发启动的外部条件；完成正式双模式支持有两个发布门槛。**

| 门槛 | 当前状态 | 影响 |
|---|---|---|
| Wayland 候选窗承载与定位 | 未接入生产后端；选择器对 `wayland:` 明确回退，阶段 0 公开 API 与桌面验收门槛尚未通过 | 原生 Wayland 浏览器仍显示 Fcitx 默认面板；这是双模式的主要技术阻塞 |
| 自绘性能 | 离线系统字体最大预热 p95 3.330 ms、固定字体 4.477 ms；实际 XCB 48 场景最大 p95 580.506 ms，尾延迟集中在同步 checked commit，可见时间仍未测量 | 实际提交未达到 p95 ≤ 5 ms、p99 ≤ 10 ms，不得将 `auto` 默认切到自绘 |
| X11 真实应用覆盖 | XCB 窗口冒烟及隔离 GTK4 输入、点击、焦点与合成器退出回退通过；真实桌面移动、缩放、多屏和应用矩阵仍需验收 | X11 还不能标记为正式支持 |

Wayland 不能通过把 XCB 窗口换成普通 `xdg_toplevel` 解决。候选窗必须使用输入法 popup/input-panel 的 compositor 语义，否则无法同时保证跟随光标、不抢焦点、受屏幕边界约束和鼠标交互。

## 2. 目标与范围

### 2.1 正式支持目标

- X11 应用使用青简位图渲染器和 X11 非激活窗口。
- XWayland 应用沿用 X11 后端，按实际 `InputContext` display 判断，不按 `XDG_SESSION_TYPE` 猜测。
- 原生 Wayland 应用使用 Wayland popup/input-panel 后端，候选内容、颜色、排版和交互与 X11 共用 `qingjian-render`。
- GNOME 原生 GTK、Qt、Chromium、Firefox、Electron 和终端逐项记录支持状态；GNOME Shell 搜索框单独验收。
- 后端不可用、私密输入、字体未就绪或渲染失败时，默认 Fcitx 面板仍能工作，不重复上屏、不遗留窗口。

### 2.2 不在本次目标内

- 不修改 Core 的候选生成、排序、翻译或学习逻辑。
- 不把 GNOME Shell 扩展当成所有 Wayland compositor 的通用实现。
- 不承诺所有 compositor 都支持同样的 popup 交互；支持范围按真实验收矩阵发布。
- 不在显示协议中传输每帧 PNG 或候选文本之外的隐私数据。

## 3. 当前实现基线

| 模块 | 已有能力 | 双模式工作 |
|---|---|---|
| `crates/qingjian-render` | `Frame + Theme → 预乘 RGBA 位图`，横排/竖排、点击区域、字体回退、明暗主题 | 保持 API 与像素模型不变；只修性能和必要的 buffer 格式转换 |
| `apps/linux/render-ffi` | C ABI v1，结果句柄、图片信息、命中区域、义项曝光 | 继续作为唯一绘制入口，不让窗口后端依赖 Engine |
| Fcitx 插件 `Controller` | 自定义面板回调、revision 校验、回退、隐私清理 | 抽象为后端选择器，增加 Wayland backend |
| XCB backend | 非激活窗口、ARGB、RandR/工作区裁限、鼠标事件 | 完成真实 X11/XWayland 验收后去掉“实验”发布标记 |
| Linux 配置 | `linux_ui.renderer = fcitx / auto / qingjian` | 增加后端能力诊断；`auto` 只选择已验收路径 |

上述表格是方案制定时的基线。现在 `Controller` 使用 [probe](../../apps/linux/fcitx5/src/panel/backend/probe.cpp) 和 [selector](../../apps/linux/fcitx5/src/panel/backend/selector.cpp) 分类实际 display，Wayland 明确回退；[CMake](../../apps/linux/fcitx5/CMakeLists.txt) 使用默认开启的 `QINGJIAN_X11_BACKEND`，最小构建可显式关闭。`auto` 的验收能力表仍为空。

## 4. 总体架构

```text
Server Frame
    │ Unix socket：候选、preedit、主题、页码
    ▼
Fcitx QingjianEngine
    │ FrameIdentity = connection generation + context UUID + revision
    ▼
PanelController
    │ probe(InputContext, display, capabilities, compositor)
    ├── X11Backend       → XCB override-redirect ARGB window
    ├── WaylandBackend   → input-panel/popup surface + wl_shm buffer
    └── FcitxFallback    → 现有 InputPanel / Classic UI
             ▲
             └── qingjian-render + render-ffi 产生同一展示位图
```

窗口后端只负责 surface、buffer、定位、输入事件和显示生命周期。所有候选数据仍来自 Server；点击和翻页回到现有 `process()`，由 identity/revision 防止旧帧误选。

建议将 `Backend` 拆为以下职责，避免 Wayland 特有状态污染 X11：

| 接口 | 责任 |
|---|---|
| `BackendProbe` | 检查 display、Fcitx frontend、compositor 协议和能力，返回可解释失败原因 |
| `SurfaceBackend` | 创建/销毁 surface，提交位图，处理 frame callback 和隐藏 |
| `PlacementBackend` | 接收光标锚点和约束，交给 compositor 定位；不暴露伪造的全局坐标 |
| `InputBackend` | 将鼠标、滚轮、关闭和协议错误转换为 Controller 事件 |
| `BackendSelector` | 按实际上下文选择 `x11`、`wayland` 或默认 Fcitx |

## 5. Wayland 路线与决策门

### 5.1 首选路线：复用 Fcitx Wayland input-panel/popup 承载

本机 Fcitx5 已安装 `wayland` 与 `waylandim` addon，系统协议文件包含 `input-method-unstable-v1`。该协议提供 input-panel overlay 语义，compositor 可以将 surface 放在输入光标附近；这比普通顶层窗口适合候选窗。

实施时必须先确认 Fcitx 5.1.x 是否向 UI addon 暴露创建该 surface 的公开 C++ API。当前仓库只能直接使用 `InputPanel::setCustomInputPanelCallback()`，该回调解决显示责任，不提供 Wayland surface 或全局坐标。不得依赖未导出的 Fcitx 私有类或复制其内部连接状态。

阶段 0 的决策条件：

1. 能通过公开 API 创建输入法 popup/input-panel surface，并把 Fcitx 的事件循环 fd 接入插件。
2. popup 的提交、隐藏、重建和 compositor 销毁都有可观察的回调。
3. 候选窗支持不抢键盘焦点；若协议不能可靠提供鼠标事件，必须明确首版交互限制，不能假装与 X11 等价。
4. GNOME 当前版本和至少一个 KDE/其他 compositor 能运行最小色块 surface，能随输入上下文切换。

若公开 API 不满足上述条件，提交一个最小的 Fcitx UI addon / 上游 API 扩展作为独立任务；不要在青简插件内链接私有符号。Wayland backend 在 API 方案确定前不进入生产构建。

### 5.2 GNOME Shell 适配

GNOME Shell 搜索框以及没有可用 input-panel surface 的场景，单独评估 Kimpanel Shell 扩展。扩展只负责承载和定位展示，不复制 Core 或候选排序；帧传输使用受限的 Unix socket/memfd，并带连接代次、上下文 UUID、revision 和尺寸上限。

扩展路径必须满足：扩展缺失、版本不匹配、Shell 重启或连接断开时立即隐藏并回退默认面板；安装包不应未经用户选择自动安装扩展。Shell 适配不能被写成“所有 Wayland 应用已支持”。

### 5.3 不采用的路线

- 普通 `xdg_toplevel` 浮窗：没有输入法 popup 语义，焦点和定位不可靠。
- 仅使用 `DISPLAY=:0` 的 XWayland 窗口：只能覆盖 XWayland 客户端，不能覆盖原生 Wayland 浏览器。
- 仅把 Fcitx Classic UI 换主题：可调整部分颜色和字号，但不能提供青简的逐片段排版、统一位图和可靠定位。
- 依赖 `wlr-layer-shell` 作为 GNOME 通用方案：它是 compositor 扩展，不是所有桌面的基础协议。

## 6. 后端实现要求

### 6.1 X11/XWayland 正式化

- 将 XCB 从“实验选项”改为可发布的 X11 backend；保留构建开关只用于没有 XCB 开发包的最小安装。
- 连接使用 `InputContext::display()` 报告的 X display；禁止使用全局 `DISPLAY` 替代。
- 保持 override-redirect、ARGB8888、合成器检测、RandR/工作区裁限和非激活窗口语义。
- 使用 cursorRect 的物理坐标；记录客户端 scale、窗口移动/缩放、多屏和负坐标的真实结果。
- 贴图失败、X connection error、RandR 变化和 compositor 消失都隐藏自绘窗并触发默认面板刷新。
- X11 真实应用验收通过后，`auto` 才允许选择该后端。

### 6.2 Wayland buffer 与事件循环

- 优先使用 `wl_shm` 双缓冲或三缓冲；缓冲区按 compositor 支持的 `wl_shm_format` 明确选择，不能假定 RGBA/BGRA 字节序。
- `qingjian-render` 输出保持预乘格式，转换集中在 Wayland backend；提交前校验 stride、尺寸和 alpha。
- 使用 Fcitx 主事件循环处理 Wayland fd、`wl_callback`、buffer release 和协议错误，不在按键处理里阻塞 `wl_display_roundtrip`。
- 新帧提交前撤销旧 frame callback；旧 buffer release 后才能复用，避免闪烁和 use-after-free。
- popup 的宽高、scale、fractional-scale 和输出切换以 compositor 事件为准；不按桌面环境变量乘二次 scale。
- 可用区域由 popup positioner/compositor 约束；窗口贴边、翻到光标上方和跨输出由协议行为决定并记录。

### 6.3 交互与生命周期

- Controller 在 `reset`、deactivate、失焦、隐私变化、Server 断线、Fcitx 重启和 compositor 销毁时同步隐藏 surface、释放 buffer、作废 identity。
- 自绘面板和 Fcitx 默认面板不能同时可见；回退顺序为隐藏自绘、解除 callback、刷新默认面板。
- 点击候选、翻页和滚轮必须带当前 identity；协议没有 pointer focus 时，至少保证键盘选择完整可用，并在支持矩阵中标出鼠标限制。
- 私密输入不缓存 Frame、不提交到 Shell 扩展、不回报曝光；日志只记录后端状态和耗时，不记录候选文本。
- Server 继续使用现有 `DisplayAcknowledged`；展示义项以实际完整可见片段为准，不因后端不同重复学习。

## 7. 配置与发布策略

继续保留：

```toml
[linux_ui]
renderer = "fcitx" # fcitx | auto | qingjian
```

- `fcitx`：强制默认面板，作为诊断和紧急回退。
- `auto`：根据真实验收矩阵选择已通过的 X11 或 Wayland backend；未知 compositor 回退。
- `qingjian`：请求青简自绘，但任何能力、定位、渲染或协议失败都回退并记录原因。

正式发布前默认仍为 `fcitx`。以下条件全部满足后，才把新安装的默认值改为 `auto`：X11/XWayland 以及声明支持的原生 Wayland 场景均有真实记录；失败路径回退可靠；性能门槛达标；安装包包含所有运行时依赖。

不要按 `XDG_SESSION_TYPE` 直接选后端。候选选择顺序应是：`InputContext::display()` 类型 → Fcitx Wayland frontend/compositor capability → 后端 probe → 默认面板。

## 8. 性能计划

当前长文本竖排 p95 约 31 ms，发布目标为预热后 UI 新增工作 p95 ≤ 5 ms、p99 ≤ 10 ms；冷启动单独记录，不把字体初始化成本藏进按键延迟。

按以下顺序优化并测量：

1. 将 `preferred_size` 与 `draw` 之间重复的文本整形结果缓存，避免同一帧反复 shape。
2. 为字体、glyph raster、布局测量和背景位图建立有界缓存；字体扫描和初始化不得发生在首个按键路径。
3. 对未变化的 preedit、候选行和主题复用布局；仅高亮/页码变化时增量提交。
4. 将 `wl_shm`/XCB upload 与 raster 分开计时，限制一次提交的最大面积，避免无意义的全屏位图。
5. 用固定许可字体和真实系统字体分别测 p50/p95/p99，覆盖 1×、1.25×、1.5×、2×、长文本 CJK、emoji 和多义项。

验收数据至少包含：Frame ready → layout、raster、buffer upload、surface commit、首帧可见的时间；现有 socket 同步等待单独统计，不能把 IPC 延迟算成绘制性能。

## 9. 分阶段实施与估算

### 阶段 0：Wayland API 与最小 surface 探针，2–3 个工作日

确认 Fcitx 5.1.x 公开 API、协议版本、GNOME/KDE compositor 能力和鼠标事件语义。产出 API 结论、最小色块程序、Wayland/XWayland/GNOME Shell 支持表。若只能依赖私有 Fcitx API，立即转为上游 API 扩展任务。

### 阶段 1：X11/XWayland 正式化，2–4 个工作日

抽象 Backend，移除 XCB 正式路径中的实验标记，完成 Chromium/Firefox、GTK、Qt、Electron、终端的 XWayland 验收，以及多屏、缩放、全屏、焦点、鼠标和回退。

### 阶段 2：Wayland popup backend，5–8 个工作日

实现公开 API 允许的 surface、wl_shm buffer、positioner、事件循环、scale、pointer、隐藏和重建；接入真实 Frame 与共享渲染器。先支持一个 GNOME 路径，再扩展其他 compositor。

### 阶段 3：GNOME/KDE 与浏览器矩阵，4–7 个工作日

分别验收 GTK4、Qt6、Chromium、Firefox、Electron、终端和 GNOME Shell；记录原生 Wayland 与 XWayland 两条路径，不用一个浏览器结果代表所有应用。完成 Kimpanel/扩展适配或明确其不在默认覆盖范围。

### 阶段 4：性能、可靠性和回退，3–5 个工作日

完成渲染缓存、buffer 生命周期、协议错误、Server/Fcitx/Shell 重启、旧帧、隐私和曝光回归。只有性能目标和回退验收同时通过，才允许 `auto` 选择对应后端。

### 阶段 5：打包和发布，2–4 个工作日

更新 Debian/Ubuntu 依赖、安装/卸载、运行时协议文件、配置说明、支持矩阵和诊断日志；从全新用户目录安装并验证默认面板仍可恢复。

预计总计 **16–31 个工作日**。如果需要修改 Fcitx 公共 API 或维护 GNOME Shell 扩展，另加约 1–3 周，并按独立项目重新排期。

## 10. 验证矩阵

| 类别 | 必测组合 | 通过要求 |
|---|---|---|
| X11 | 原生 X11 GTK/Qt/浏览器/终端 | 青简自绘、光标跟随、非激活、鼠标和分页可用 |
| XWayland | GNOME Wayland 会话中强制 X11 的 Chromium/Firefox/GTK/Qt | 走 XCB，位置不漂移，切换回原生 Wayland 不残留窗口 |
| GNOME Wayland | GTK4、Qt6、Chromium、Firefox、Electron、终端 | 走 Wayland popup；逐项记录 compositor/frontend/scale |
| KDE/其他 Wayland | 至少一个 KDE 和一个非 GNOME compositor | 能力不同时允许回退，不能误标为通用支持 |
| Shell | GNOME Activities 搜索框 | 单独验证扩展/系统面板；扩展缺失时干净回退 |
| 几何 | 移动、调整大小、四边、多屏、负坐标、100/125/150/200%、分数缩放、全屏 | 不抢焦点、不出屏、不双窗、不随旧坐标漂移 |
| 视觉 | 浅/深色、横/竖排、长 CJK、日文读音、emoji、生词、空槽、多义项 | X11 与 Wayland 使用同一展示模型和参考图 |
| 生命周期 | reset、失焦、切换输入法、Server/Fcitx/compositor 重启、旧帧点击 | 恰好上屏一次，无残留 surface 或错误选词 |
| 隐私 | Password、Sensitive、Disable、未知能力、能力动态变化 | 不显示、不缓存、不曝光、不写候选日志 |
| 性能 | 冷启动、预热、长短文本、1×/2×、X11/Wayland upload | 预热 p95 ≤ 5 ms、p99 ≤ 10 ms，端到端数据可复现 |

自动化测试继续覆盖 Rust workspace、render-ffi C ABI、CMake/CTest、帧 identity、命中区域和回退。真实桌面测试必须保存应用版本、display、frontend、scale、坐标、截图和日志；无桌面 CI 只能作为必要条件，不能替代 Wayland 定位验收。

## 11. 完成定义

只有以下条件全部满足，才可把 Linux 双模式标记为正式支持：

- X11、XWayland 和声明支持的原生 Wayland 场景都有实际应用验收记录。
- 所有通过场景均使用共享青简渲染器，候选内容、排版、颜色和窗口生命周期一致。
- Wayland popup 定位由 compositor/input-panel 语义提供，不依赖固定坐标、鼠标位置或普通顶层窗口。
- 键盘、鼠标、分页、缩放、失焦、输入法切换、Server/Fcitx/compositor 重启均通过；不支持的交互明确记录。
- 私密输入、曝光记录、学习、失焦上屏和默认面板回退与原链路一致。
- 预热性能达到 p95/p99 目标，包含 X11 和 Wayland 的实际上传耗时。
- 安装包、依赖、配置、诊断和卸载文档完成同步，`auto` 默认值只覆盖已验收后端。

在这些条件完成前，浏览器原生 Wayland 继续显示默认 Fcitx 面板是预期回退行为，不应把它报告为青简自绘支持。
