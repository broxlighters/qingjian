# Fcitx5 支持方案

## 结论

目前没有阻塞开发的技术问题。`qingjian-core::Engine` 已经提供平台层需要的输入、查询、上屏、学习和私密输入接口；Fcitx5 也允许以共享库形式注册输入法引擎。Ubuntu 26.04 + Fcitx5 5.1.19 属于可支持的目标环境。

首版已固定两项边界：

1. 第一版使用「Fcitx5 插件 + 独立 Rust Server」结构，不把完整 Engine 直接链接进 Fcitx5 守护进程。
2. 第一版使用 Fcitx5 自带候选 UI，通过候选 label / annotation 显示译词；macOS / Windows 的自绘候选窗列入后续阶段。

以下事项是开发前置条件或产品取舍，不构成阻塞：

- 本机已具备 Fcitx5 5.1.19 开发头文件。其他开发机需要安装 `libfcitx5core-dev`、`libfcitx5config-dev`、`libfcitx5utils-dev`、`nlohmann-json3-dev`、CMake 和 C++20 编译器。
- 仓库已新增 `apps/linux`、用户目录安装/卸载脚本和 Linux CI 插件检查；正式发行包尚未生成。
- Fcitx5 默认 UI 无法完全复刻现有自绘窗口，因此第一版的视觉效果会与 macOS / Windows 不同。

## 目标与非目标

第一版目标是让用户在 GNOME/Wayland 下的 GTK、Qt、Electron 和终端应用中完成稳定的中文拼音输入：

- 全拼输入和 preedit 显示
- 词级候选、整句候选
- 数字键选词、空格、回车、退格、Delete、Esc
- 候选翻页和方向键选择
- 中文标点
- 用户词频、用户词和个人 n-gram 落盘
- 候选旁的一条译词及生词状态
- 密码 / 私密输入框禁用学习、日志和云请求
- Fcitx5 配置工具中可添加和移除「青简」

第一版暂不承诺：自绘候选窗口、设置 GUI、云联想、翻译选中文字、后台神经重排、完整英文候选、所有应用的特殊行为适配。这些能力可以在基础输入链路稳定后接入。

## 总体结构

```text
应用（GTK / Qt / Electron / Wayland）
              │ Fcitx5 frontend
              ▼
Fcitx5 青简插件（C++，只做适配和显示）
              │ Unix domain socket，长度前缀 JSON
              ▼
qingjian-linux-server（Rust）
              │
              ▼
qingjian-core::Engine + dictionary / translate / learning / lm
```

Fcitx5 插件运行在 Fcitx5 进程中，只处理 `InputMethodEngineV2::keyEvent`、输入上下文和候选显示。Server 负责持有 Engine、加载产品数据、维护会话，并将 `Frame` 转成插件所需的候选列表。

这种结构沿用 Windows 的 Server 思路，避免 Rust panic、模型加载或词库错误直接拖垮 Fcitx5 守护进程，也保持 Core 与平台层解耦。Fcitx5 官方的插件目录和注册文件约定见[官方教程](https://fcitx-im.org/wiki/Develop_an_simple_input_method)。

## 代码组织

建议新增以下目录和 crate：

```text
apps/linux/
├── server/                 # Rust：Engine 装配、Unix socket、会话分派
└── fcitx5/                # C++：InputMethodEngineV2 插件和 Fcitx5 元数据
    ├── CMakeLists.txt
    ├── src/qingjian.cpp
    ├── src/qingjian.h
    ├── data/addon/qingjian.conf
    └── data/inputmethod/qingjian.conf
```

`apps/linux/server` 可以复用 `apps/windows/server` 的装配逻辑、`qingjian-platform::protocol` 的可序列化帧类型和 `qingjian-learning` 的用户数据路径，但不能引入 Windows API。协议中与 Win32 相关的命名管道和屏幕矩形字段应抽成平台无关部分，Linux 传输层单独实现 Unix socket。

Fcitx5 插件安装到用户目录时，目标文件为：

```text
~/.local/lib/fcitx5/qingjian.so
~/.local/share/fcitx5/addon/qingjian.conf
~/.local/share/fcitx5/inputmethod/qingjian.conf
```

正式发行时再决定发行版包格式（Deb、Flatpak 或 tar 包）。

## 输入事件映射

插件收到按键后，按以下顺序处理：

1. 释放事件直接返回，不重复送入 Core。
2. 若处于候选选择状态，数字键、上下键、PageUp/PageDown、Tab 和确认键先交给当前候选处理。
3. 字母、撇号、退格、Delete、空格、回车和中文标点转换成 `qingjian-platform::protocol::KeyEvent`。
4. Server 返回 `KeyOutcome` 后，插件决定 `filterAndAccept()`、`forwardKey()` 或提交字符串。
5. 每次状态变化调用 Fcitx 的 preedit 和候选列表更新接口。

Core 的调用关系应保持简单：

```text
按键 → Engine::push / backspace
     → Engine::query
     → Engine::annotate
候选确认 → Engine::commit / commit_translation
失焦或切换输入法 → Engine::take_raw / break_chain
```

Fcitx5 的输入上下文可提供光标矩形、能力标志和焦点变化；这些信息用于候选定位、私密输入判断和会话关闭。输入上下文接口参考：[Fcitx5 InputContext](https://github.com/fcitx/fcitx5/blob/master/src/lib/fcitx/inputcontext.h)。

## 候选和译词显示

第一版使用 `CommonCandidateList` 和 Fcitx5 默认 UI：

- 候选正文放 `CandidateWord` 的主文本
- 译词和词性放候选的附加 label / annotation
- 当前页和高亮索引由 Fcitx5 候选列表处理
- `Frame::layout` 在第一版统一映射成默认横排布局

由于默认 UI 的主题和布局由 Fcitx5 控制，不能保证译词颜色、生词橙色、竖排布局和自绘阴影与其他平台一致。若用户反馈显示空间不足，再实现独立 UI addon 或调整候选文本的紧凑格式；不要为了第一版视觉一致性修改 Core 的 Candidate 数据模型。

## 配置和数据

Linux 使用与其他平台相同的 TOML 配置模型，路径遵循 XDG：

```text
配置：${XDG_CONFIG_HOME:-~/.config}/qingjian/config.toml
数据：${XDG_DATA_HOME:-~/.local/share}/qingjian/
日志：${XDG_STATE_HOME:-~/.local/state}/qingjian/logs/
```

Server 启动时按现有 `AssemblySpec` 装载：主词库、领域词库、释义表、英文词表、emoji、`lm.qj` 和用户学习数据。第一版云联想保持关闭；模型文件虽然可以随包提供，但在 Linux 壳稳定前不默认启用。

密码框和私密输入框由 Fcitx 输入上下文能力标志判断，并映射到 `Engine::set_private(true)`。判断失败时应选择不发送、不学习的安全路径，并在测试中记录具体应用行为。

## 分阶段计划

### 阶段 0：环境和协议整理

- 安装并确认 Fcitx5 开发依赖。
- 将 Windows 协议中的传输层抽象为 `Read + Write`，确保 Server 测试不依赖命名管道。
- 定义 Linux 的 `OpenSession`、`Key`、`Update`、`Commit`、`CloseSession` 消息流。
- 确定单个 Server 进程管理多个 Fcitx InputContext 的会话表。
- 为 Unix socket 增加连接失败、Server 重启和协议版本不匹配的错误处理。

### 阶段 1：Fcitx5 最小插件

- 用固定候选实现插件注册和加载。
- 加入 addon 与 inputmethod 配置文件。
- 验证 Fcitx5 配置工具可以添加「青简」。
- 验证按键拦截、preedit、提交文本和 Esc 清空。

验收：在 GNOME 文本编辑器中可以输入固定文本，切换到其他输入法后插件正确释放状态。

### 阶段 2：接入 Core 和默认候选 UI

- 新建 Linux Server，装配正式词库和释义表。
- 接入全拼、候选选择、分页、退格和中文标点。
- 将 Core 的 `Frame` 转成 Fcitx 候选列表和 preedit。
- 接入用户学习数据、输入日志和原子写入。
- 增加 CLI/Server 协议回环测试和 Fcitx 插件最小集成测试。

验收：`nihao`、`kaifa`、`jintiantianqihenhao`、`nihooma` 在 GTK、Qt 和一个 Electron 应用中分别通过；选词后重启 Server，词频仍然生效。

### 阶段 3：补齐输入体验

- 双拼、模糊音、英文模式和快捷候选。
- 译词 annotation、生词记录和译词快捷键。
- 应用能力判断、密码框和私密输入。
- 候选窗口定位、缩放和不同主题的可读性调整。

验收：常用桌面应用、浏览器、终端、IDE 和密码框各完成一轮测试；输入延迟和崩溃恢复有记录。

### 阶段 4：可选增强和发布

- 接入异步云联想和释义兜底。
- 接入本地整句模型，记录加载时间和每次重排延迟。
- 评估是否需要独立 Fcitx UI addon。
- 添加 Linux CI、安装脚本、卸载脚本、版本信息和数据校验。
- 生成发行包并验证全新用户目录的首次启动。

## 测试矩阵

至少覆盖以下组合：

| 类别 | 应用 / 场景 |
|---|---|
| GTK | GNOME Text Editor、Firefox |
| Qt | KDE/Qt 示例应用或系统设置 |
| Electron | VS Code 或其他 Electron 应用 |
| 终端 | GNOME Terminal、一个 Wayland 原生终端 |
| 输入状态 | 普通文本、密码框、只读控件、选区失焦、应用退出 |
| 桌面协议 | Wayland 原生、XWayland |

功能测试沿用 Core 的已有用例，并新增插件层测试：候选确认顺序、失焦清理、Server 断线重连、输入法切换、重复按键、修饰键、候选分页和 Fcitx 重启。

性能目标沿用 CLI 的每键约 10 ms 目标。插件到 Server 的 IPC、候选转换和 Fcitx UI 更新应单独计时；云联想和本地模型不能阻塞按键回调。

## 风险和决策门

- **候选 UI 不足**：如果默认 UI 无法稳定显示译词，先压缩 annotation 文本；只有影响可用性时才立项自定义 UI。
- **插件崩溃影响全局输入**：C++ 边界不允许 Rust panic 穿出；Server 崩溃时插件应停止拦截按键并允许用户继续原样输入。
- **Wayland 光标信息不完整**：候选定位失败时退回 Fcitx 默认定位，不阻塞输入；按应用记录例外。
- **多会话状态错乱**：所有 Engine 状态放 Server 的 `SessionId` 下，插件不得把拼音缓冲存在全局单例中。
- **ABI 和发行版差异**：首个支持目标固定为 Ubuntu 26.04 / Fcitx5 5.1.x；其他发行版在 CI 和实际机器验证后再声明支持。
- **数据和模型体积**：词库、释义和模型与程序分开校验、安装；数据缺失时明确降级到样例或禁用对应功能。

## 完成定义

当以下条件全部满足时，才把 Linux 从“规划”改为“测试版可用”：

- Fcitx5 配置工具能添加并启用青简。
- GTK、Qt、Electron、Wayland 原生终端各有一次成功输入记录。
- 普通文本和密码框的隐私行为符合预期。
- Server 重启不会让 Fcitx5 崩溃，断线时按键可以恢复原样传递。
- Core 现有测试、Linux Server 测试和插件构建检查全部通过。
- 安装、卸载、数据目录、日志目录和版本信息都有可复现的说明。

## 当前实现进度

阶段 0–2 的代码链路已接入：Unix socket 版本握手、跨连接会话编号隔离、断线回收、独立组句状态、Fcitx5 元数据、完整 preedit/候选页、数字/方向/翻页/鼠标选择、学习持久化、XDG 数据与日志、安装/卸载及无桌面集成测试。阶段 3 已复用双拼、模糊音、英文候选、自定义短语、译词快捷键和私密输入；使用 Fcitx 默认候选定位与主题。云/神经/独立 UI 仍属后续。

已在本机 Fcitx5 5.1.19 头文件下编译插件，并通过 Server 与无桌面 InputContext 集成测试。GTK、Qt、Electron、Wayland 原生终端实际前端行为和性能记录尚未完成，因此 Linux 仍是开发验证阶段，未达到“测试版可用”的完成定义。复现命令与限制见 [Linux 工程记录](../notes/linux-fcitx5.md)。
