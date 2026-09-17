# Linux X11 / Wayland 双模式自绘候选窗开发方案

评估日期：2026-09-16。目标环境：Ubuntu 26.04、Fcitx5 5.1.19、GNOME Shell 50.1；当前会话为 Wayland，同时提供 XWayland。

本文件是完成双模式正式支持的开发计划，不表示目标功能已经完成。当前实现、测试记录与已知限制见 [Linux 自绘候选面板支持矩阵](../notes/linux-ui-support.md)。

2026-09-17 路线修订：指定 Ubuntu/GNOME 版本下的应用默认自绘，执行 [GNOME 自绘正式支持方案](linux_gnome_custom_ui_plan.md)。GNOME 原生应用以配套 Shell 位图扩展为主线，阶段、默认策略和验收以新方案为准；本文的通用 input-method popup 路线仅针对实际提供相应能力的其他 compositor，不再要求 GNOME 通过该协议验收。

2026-09-17 实施进度：已落地后端选择/诊断、可选 X11 构建、合成器故障回退与文字缓存优化；Wayland 独立 registry/色块探针已构建并在本机运行。阶段 0 **未通过**：本机普通 GNOME 连接没有 v1/v2 input-panel 全局，KDE/其他 compositor 尚未验收。已将第 5.1 节的 API 扩展转为固定 Fcitx 5.1.19 的本地 popup API 提案，完成公开头、同连接 surface/role、失效回调、生命周期/ASan 和消费者编译验证；提案未发送上游、未进入生产，真实 GNOME/KDE gate 仍未通过。公开连接 API 的能力、限制及独立 API 扩展任务见 [核验记录](../notes/linux-wayland-api.md)。阶段 2/3 不能标为完成；离线性能改善不能替代窗口上传和真实应用验收。

## 1. 结论与阻塞点

**可以启动验证性开发；展示通路的技术前置尚未解除，正式支持还需性能和真实应用验收。** GNOME 的具体前置及解除条件见新方案，不能把“允许开始验证”理解为已无阻塞。

| 门槛 | 当前状态 | 影响 |
|---|---|---|
| Wayland 候选窗承载与定位 | 未接入生产后端；选择器对 `wayland:` 明确回退，阶段 0 公开 API 与桌面验收门槛尚未通过 | 原生 Wayland 浏览器仍显示 Fcitx 默认面板；这是双模式的主要技术阻塞 |
| 自绘性能 | 离线系统字体最大预热 p95 3.330 ms、固定字体 4.477 ms；实际 XCB 48 场景最大 p95 580.506 ms，尾延迟集中在同步 checked commit，可见时间仍未测量 | 实际提交未达到 p95 ≤ 5 ms、p99 ≤ 10 ms，不得将 `auto` 默认切到自绘 |
| X11 真实应用覆盖 | XCB 窗口冒烟及隔离 GTK4 输入、点击、焦点与合成器退出回退通过；真实桌面移动、缩放、多屏和应用矩阵仍需验收 | X11 还不能标记为正式支持 |

Wayland 不能通过把 XCB 窗口换成普通 `xdg_toplevel` 解决。提供相应协议的 compositor 使用输入法 popup/input-panel；本机 GNOME 采用 Shell 展示与定位适配，分别验证光标跟随、非激活、屏幕边界和交互。

## 2. 目标与范围

### 2.1 正式支持目标

- X11 应用使用青简位图渲染器和 X11 非激活窗口。
- XWayland 应用沿用 X11 后端，按实际 `InputContext` display 判断，不按 `XDG_SESSION_TYPE` 猜测。
- GNOME 原生 Wayland 应用使用 Shell 位图展示，其他 compositor 按实际能力使用 popup/input-panel；候选内容、颜色、排版和交互与 X11 共用 `qingjian-render`。
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
| `crates/qingjian-render` | `Frame + Theme → 预乘 RGBA 位图`，横排/竖排、点击区域、字体回退、明暗主题 | 保持共享主题默认值与像素模型；Linux 尺寸解析、主题副本和必要的 FFI 参数按 GNOME 专项方案实施 |
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
    ├── GnomeBackend     → 展示桥接 + GNOME Shell 位图扩展
    ├── WaylandBackend   → 支持相应协议的 compositor：input-panel/popup + wl_shm
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
| `BackendSelector` | 按实际上下文、frontend 与握手能力选择 XCB、GNOME Shell、其他 Wayland popup 或默认 Fcitx |

## 5. Wayland 路线与决策门

### 5.1 其他 compositor 路线：复用 Fcitx Wayland input-panel/popup 承载

本机已安装 addon 和协议文件不证明 compositor 提供对应能力。仅在目标 compositor 的真实 Fcitx 连接可用时研究 input-panel overlay / input-popup；GNOME 当前能力记录不满足这条路线。

实施时必须先确认 Fcitx 5.1.x 是否向 UI addon 暴露创建该 surface 的公开 C++ API。当前仓库只能直接使用 `InputPanel::setCustomInputPanelCallback()`，该回调解决显示责任，不提供 Wayland surface 或全局坐标。不得依赖未导出的 Fcitx 私有类或复制其内部连接状态。

阶段 0 的决策条件：

1. 能通过公开 API 创建同连接的输入法 popup/input-panel surface；借用 Fcitx 连接时由 Fcitx 保持唯一 reader，插件不得另建 fd reader 或自行 dispatch。
2. popup 的提交、隐藏、重建和 compositor 销毁都有可观察的回调。
3. 候选窗支持不抢键盘焦点；若协议不能可靠提供鼠标事件，必须明确首版交互限制，不能假装与 X11 等价。
4. 声明支持的目标 compositor 能在其真实输入法连接运行最小色块 surface，随输入上下文切换；普通新连接的 registry 结果不能替代该验证。

若公开 API 不满足上述条件，提交一个最小的 Fcitx UI addon / 上游 API 扩展作为独立任务；不要在青简插件内链接私有符号。Wayland backend 在 API 方案确定前不进入生产构建。

### 5.2 GNOME 主线：Shell 位图展示

GNOME 普通原生应用按 [专项方案](linux_gnome_custom_ui_plan.md) 实现配套展示扩展，不将其仅作为搜索框补充。扩展负责承载和定位，不复制 Core 或候选排序；位图传输、异步展示确认、显示所有权、缩放及焦点代次按新方案验证并冻结。现成 Kimpanel 可作为定位参考，安装它不等于接入青简位图。

扩展缺失、版本不匹配、重新登录或连接断开时撤窗并恢复默认面板，正常场景持续回退计为未通过。完整 GNOME 安装包括扩展及启用步骤；不在系统包脚本中强改用户扩展状态或重启 Shell。搜索框另验，其他桌面不由该结果推定支持。

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

已反馈的自绘尺寸偏小也是本次必修项，按 [GNOME 专项方案第 6.4、9.4 节](linux_gnome_custom_ui_plan.md) 采集缩放来源、修复现有 X11/XWayland 路径并完成尺寸验收。不能仅把 `InputContext::scaleFactor()` 当作完整显示缩放，不能将 Xft DPI、屏幕倍率和客户端倍率相乘；必测本机约 167% + 100% 混合缩放、系统文字放大与用户 UI 大小配置。该任务可先于 Shell 扩展独立推进。

### 6.2 Wayland buffer 与事件循环

- 优先使用 `wl_shm` 双缓冲或三缓冲；缓冲区按 compositor 支持的 `wl_shm_format` 明确选择，不能假定 RGBA/BGRA 字节序。
- `qingjian-render` 输出保持预乘格式，转换集中在 Wayland backend；提交前校验 stride、尺寸和 alpha。
- 由 Fcitx 现有主事件循环读取借用连接，插件只处理公开 API 回调和自有对象事件；不新增第二 reader，不自行 roundtrip/dispatch，不阻塞按键处理。
- 新帧提交前撤销旧 frame callback；旧 buffer release 后才能复用，避免闪烁和 use-after-free。
- popup 的宽高、scale、fractional-scale 和输出切换以 compositor 事件为准；不按桌面环境变量乘二次 scale。
- 可用区域由 popup positioner/compositor 约束；窗口贴边、翻到光标上方和跨输出由协议行为决定并记录。

### 6.3 交互与生命周期

- Controller 在 `reset`、deactivate、失焦、隐私变化、Server 断线、Fcitx 重启和 compositor 销毁时同步隐藏 surface、释放 buffer、作废 identity。
- 自绘面板和 Fcitx 默认面板不能同时可见；回退顺序为隐藏自绘、解除 callback、刷新默认面板。
- 点击候选、翻页和滚轮必须带当前 identity；协议没有可靠 pointer 交互时记为有限支持，不满足本次普通输入框的正式支持目标。
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

不要按 `XDG_SESSION_TYPE` 直接选后端。结合上下文 display、frontend、坐标能力及实际后端握手选择；GNOME IBus 上下文不能只因缺少 `wayland:` 前缀被拒绝。GNOME 新安装默认策略和旧配置迁移按专项方案执行。

## 8. 性能计划

31 ms 是早期离线基线。2026-09-17 离线系统字体最大预热 p95 已为 3.330 ms，但实际 XCB 提交仍有长尾，见支持矩阵。保留预热后 UI 新增处理工作 p95 ≤ 5 ms、p99 ≤ 10 ms；冷启动、IPC 和屏幕可见延迟分别统计，GNOME 的异步口径及新增门槛按专项方案执行。

按以下顺序优化并测量：

1. 将 `preferred_size` 与 `draw` 之间重复的文本整形结果缓存，避免同一帧反复 shape。
2. 为字体、glyph raster、布局测量和背景位图建立有界缓存；字体扫描和初始化不得发生在首个按键路径。
3. 对未变化的 preedit、候选行和主题复用布局；仅高亮/页码变化时增量提交。
4. 将 `wl_shm`/XCB upload 与 raster 分开计时，限制一次提交的最大面积，避免无意义的全屏位图。
5. 用固定许可字体和真实系统字体分别测 p50/p95/p99，覆盖 1×、1.25×、1.5×、2×、长文本 CJK、emoji 和多义项。

验收数据至少包含：Frame ready → layout、raster、buffer upload、surface commit、首帧可见的时间；现有 socket 同步等待单独统计，不能把 IPC 延迟算成绘制性能。

## 9. 分阶段实施与估算

以下保留通用 popup 与跨桌面的原排期参考，不用于本次 GNOME 交付估算；本次依赖顺序和工作量以专项方案第 11 节为准。

### 阶段 0：Wayland API 与最小 surface 探针，2–3 个工作日

确认 Fcitx 公开 API、目标 compositor 协议版本和鼠标事件语义。产出 API 结论、最小色块程序与真实连接记录。若只能依赖私有 Fcitx API，转为独立公开 API 提案；GNOME 不等待此项完成。

### 阶段 1：X11/XWayland 正式化，2–4 个工作日

抽象 Backend，移除 XCB 正式路径中的实验标记，完成 Chromium/Firefox、GTK、Qt、Electron、终端的 XWayland 验收，以及多屏、缩放、全屏、焦点、鼠标和回退。

### 阶段 2：Wayland popup backend，5–8 个工作日

实现目标协议允许的 surface、wl_shm buffer、定位语义、回调、scale、pointer、隐藏和重建，接入真实 Frame 与共享渲染器；先在提供该协议的一个 compositor 验收，不要求 GNOME 走相同协议。

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
| GNOME Wayland | 按专项方案冻结的 Chrome、Firefox、VS Code、GTK4、Qt6、终端及适用应用 | 走 Shell 位图展示；逐项记录 frontend、坐标类型、scale 和实际后端 |
| KDE/其他 Wayland | 至少一个 KDE 和一个非 GNOME compositor | 能力不同时允许回退，不能误标为通用支持 |
| Shell | GNOME Activities 搜索框 | 独立扩展项；不纳入普通输入框必交付结论 |
| 几何 | 移动、调整大小、四边、多屏、负坐标、100/125/150/约 167/200%、分数缩放、全屏 | 不抢焦点、不出屏、不双窗、不随旧坐标漂移；混合缩放尺寸及命中正确 |
| 尺寸 | 本机约 167% + 100% 双屏、Fcitx 对照、原生 Wayland/XWayland 对照、文字与 UI 大小配置 | 已反馈偏小问题得到验证和修复；不漏缩放、不重复放大；按专项方案尺寸基准验收 |
| 视觉 | 浅/深色、横/竖排、长 CJK、日文读音、emoji、生词、空槽、多义项 | X11 与 Wayland 使用同一展示模型和参考图 |
| 生命周期 | reset、失焦、切换输入法、Server/Fcitx/compositor 重启、旧帧点击 | 恰好上屏一次，无残留 surface 或错误选词 |
| 隐私 | Password、Sensitive、Disable、未知能力、能力动态变化 | 不显示、不缓存、不曝光、不写候选日志 |
| 性能 | 冷启动、预热、长短文本、1×/2×、X11/Wayland upload | 预热 p95 ≤ 5 ms、p99 ≤ 10 ms，端到端数据可复现 |

自动化测试继续覆盖 Rust workspace、render-ffi C ABI、CMake/CTest、帧 identity、命中区域和回退。真实桌面测试必须保存应用版本、display、frontend、scale、坐标、截图和日志；无桌面 CI 只能作为必要条件，不能替代 Wayland 定位验收。

## 11. 完成定义

只有以下条件全部满足，才可把 Linux 双模式标记为正式支持：

- X11、XWayland 和声明支持的原生 Wayland 场景都有实际应用验收记录。
- 所有通过场景均使用共享青简渲染器，候选内容、排版、颜色和窗口生命周期一致。
- 原生 Wayland 定位来自已验证的 GNOME Shell 适配或 compositor/input-panel 语义，不依赖固定坐标、鼠标位置或普通顶层窗口。
- 键盘、鼠标、分页、缩放、失焦、输入法切换、Server/Fcitx/compositor 重启均通过；不支持的交互明确记录。
- 私密输入、曝光记录、学习、失焦上屏和默认面板回退与原链路一致。
- 预热性能达到 p95/p99 目标，包含 X11 和 Wayland 的实际上传耗时。
- 安装包、依赖、配置、诊断和卸载文档完成同步，`auto` 默认值只覆盖已验收后端。

在这些条件完成前，浏览器原生 Wayland 继续显示默认 Fcitx 面板是预期回退行为，不应把它报告为青简自绘支持。
