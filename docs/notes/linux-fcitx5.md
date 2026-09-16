# Linux Fcitx5 构建与验证

首版使用 C++ Fcitx5 addon + Rust Server 和 Fcitx 默认候选 UI。目标 Ubuntu 26.04 / Fcitx5 5.1.x；当前可构建并通过无桌面集成测试，GTK、Qt、Electron、Wayland 原生终端和 GNOME Shell 均未声明自绘支持，不能标记为统一 UI 测试版可用。

## 构建与安装

安装脚本同时构建 `qingjian-render-ffi` 并静态链接。普通构建关闭未经桌面验收的 XCB 原型；所有配置模式均可回退默认面板。直接 CMake 默认不链接 FFI，仍可独立运行原面板和生命周期测试。编译/链接错误会使构建失败，运行时绘制失败才执行回退。

实验构建可使用：

```bash
cargo build -p qingjian-render-ffi --locked
cmake -S apps/linux/fcitx5 -B target/fcitx5-ui \
  -DQINGJIAN_RENDER_FFI=ON -DQINGJIAN_EXPERIMENTAL_X11=ON \
  -DQINGJIAN_RENDER_FFI_LIB="$PWD/target/debug/libqingjian_render_ffi.a"
cmake --build target/fcitx5-ui --parallel 2
ctest --test-dir target/fcitx5-ui --output-on-failure
```

实验 XCB 额外需要 `libxcb1-dev libxcb-render0-dev libxcb-randr0-dev`；字体建议 `fonts-noto-core fonts-noto-cjk fonts-noto-color-emoji`。C ABI 输入最大 256 KiB，返回预乘 RGBA8；配套 result/renderer destroy 管理 Rust 内存，静态库未用部分通过 section GC 丢弃，内部 Rust 符号不向 Fcitx 导出。其配置、协议及未验收边界见 [支持矩阵](linux-ui-support.md)。

```bash
sudo apt install libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev nlohmann-json3-dev cmake g++ pkg-config
cargo test -p qingjian-linux-server --locked
cmake -S apps/linux/fcitx5 -B build/fcitx5
cmake --build build/fcitx5
ctest --test-dir build/fcitx5 --output-on-failure
apps/linux/scripts/install.sh
```

插件要求 Fcitx5 >= 5.1.9 的候选 comment API；C++20 兼容 Fcitx5 5.1.19 头文件。安装脚本默认 release，支持 `--debug`、`--prefix /绝对目录` 、`--sample` 和 `--experimental-x11`；`--debug` 同时使用 Cargo debug 和 CMake Debug 配置。`--sample` 跳过产品生成数据复制，保留样例；`--experimental-x11` 显式构建实验 XCB 后端。支持 `CARGO_TARGET_DIR` 指定 Rust 产物目录，`CMAKE_BUILD_PARALLEL_LEVEL` 控制 C++ 并行数（默认 4）。默认路径是 `~/.local/{bin,lib/fcitx5,share}`，安装后手动启动服务并在 Fcitx 配置工具中添加青简：

```bash
systemctl --user daemon-reload
systemctl --user enable --now qingjian-linux-server.service
```

不使用 systemd 时直接运行 `~/.local/bin/qingjian-linux-server`。自定义 prefix 的 service 文件需要用 `systemctl --user link /绝对前缀/share/systemd/user/qingjian-linux-server.service` 注册。Fcitx 的默认 addon 搜索路径不含 `~/.local/lib/fcitx5`；安装脚本会将插件的绝对路径写入 addon 配置的 `Library`，无需修改 Fcitx 进程环境。Fcitx 用户目录之外的 prefix 仍需要设置 data 搜索路径，以找到 addon 与输入法元数据，因此自定义前缀主要用于打包和安装测试。卸载：`apps/linux/scripts/uninstall.sh`（相同的 `--prefix`），保留用户配置、学习数据、日志。安装和卸载都不修改 Fcitx profile。

### Debian / Ubuntu 包

在 Debian/Ubuntu amd64 上可用以下命令生成完整数据包：

```bash
apps/linux/scripts/package-deb.sh
```

产物位于 `target/deb/qingjian-fcitx5_<版本>_<架构>.deb`，包含 Rust Server、Fcitx5 插件、词库、释义表、语言模型、许可证和用户级 systemd service。脚本通过 `dpkg-shlibdeps` 写入运行时依赖，并在打包前运行 24 项 Fcitx CTest；`--sample` 可生成不含产品生成数据的测试包。包内使用 `/usr/lib/<multiarch>/fcitx5/qingjian.so`，不会启用实验性 X11 自绘后端。

安装包后以桌面用户运行 `systemctl --user daemon-reload && systemctl --user enable --now qingjian-linux-server.service`，重启 Fcitx5，再在配置工具中添加「青简」。包升级和卸载都不删除用户配置、学习数据和日志；源码用户安装的 `~/.local` 插件应先按其脚本卸载，避免两个版本同时被 Fcitx 发现。当前包是开发版构建，面向与构建机相同或更新的发行版，不保证旧发行版 ABI 兼容。

## 数据与运行参数

- `QINGJIAN_RESOURCES` 指向含 `assets/`、`data/generated/` 的资源根；缺省是可执行文件所在 prefix 的 `share/qingjian/resources`，开发时回落仓库根。程序资源与用户学习数据分目录，卸载只删除 `resources/`。
- `QINGJIAN_DICT` 可覆盖主词库路径。损坏/缺失主词库回落样例并记警告，保留用户学习配置；可选释义、英文词表、LM 失败只禁用对应能力。
- `QINGJIAN_SOCKET` 覆盖 Unix socket 的绝对路径。缺省 `$XDG_RUNTIME_DIR/qingjian.sock`，无 runtime 时为 `/tmp/qingjian-<uid>/qingjian.sock`，后者目录权限 0700。父目录必须为本用户所有且其他用户不可写，socket 0600，连接双向校验同 UID。
- 配置 `${XDG_CONFIG_HOME:-~/.config}/qingjian/config.toml`，用户数据 `${XDG_DATA_HOME:-~/.local/share}/qingjian/`，诊断/输入日志 `${XDG_STATE_HOME:-~/.local/state}/qingjian/logs/`。
- 安装目录 `share/qingjian/resources/SHA256SUMS` 记录实际随包数据的校验值；在该目录运行 `sha256sum -c SHA256SUMS` 可复验。生成数据依然沿用现有工具和来源许可。
- `qingjian-linux-server --version` 输出产品版本。收到 SIGTERM / SIGINT 后，在主线程退出并刷新学习数据。

配置修改后重启 Server 生效。首版不接云服务、选区翻译和神经重排，配置中的相关开关暂不启用。

## 协议与状态

复用平台层的 4 字节小端长度前缀 JSON（最大 16 MiB）；原来的通用 `Read + Write` 编解码本来就能用于 socket，不需要额外空 trait。Linux 使用 `OpenSession` → 空 `Update` 握手、`Privacy`、`Key` → `KeyResult`、`Poll` → `Update`、`Commit` → `Committed`、`CloseSession`。协议版本不匹配关闭连接。

每条连接的本地 SessionId 在 Server 映射成唯一全局编号，断线回收所有会话。Engine 的静态数据和落盘服务保持进程内唯一；`EngineSession` 保存独立组句、历史、标点配对和学习链，Router 还保存各会话的候选页/高亮。切换时交换输入状态，避免复制词库和用户数据写入覆盖。新会话默认 private，插件报告能力后才允许学习。同一上下文 Privacy 实际改变时，当前或挂起的组句、透传、历史、学习链、候选及查询缓存都无痕丢弃；切换到另一个独立上下文只恢复其写入开关，不清组句。关闭挂起会话与退出时按各自隐私状态刷新普通透传日志，私密透传丢弃。

插件使用 InputContextProperty 管理生命周期。密码 / Disable 直接放行，Sensitive 使用 Engine 私密模式，能力全空也按私密处理。能力变化立即同步 Privacy 并清理面板，Shift / 停用 / 失焦的 Commit 使用同一入口再次刷新 Privacy；Fcitx 在能力尚未改变前发出的 CapabilityChanged 停用事件直接保守清理，不把旧组句提交到新密码框。组句消失或断线清空面板；I/O 使用共享 200 ms 截止时间并禁用 SIGPIPE，下一次按键重新握手。候选鼠标选择与翻页回到 Server，带帧序号防止旧候选回调选中新帧。

## 验证记录与未完成验收

2026-09-16 阶段 3 打包验收：在全新临时前缀和全新 XDG 配置/数据/状态目录中完成 debug 安装，`qingjian-linux-server --version` 返回 `0.1.0-dev`，随包资源 `SHA256SUMS` 全部通过；通过 Linux v1 握手及 `nihao` 上屏“你好”的 socket 验证。随后卸载，用户配置和数据标记保留，程序、插件、service 和随包资源删除。实验构建依赖 `libxcb-randr0-dev` 缺失时会在 CMake 配置阶段明确失败，不会继续安装半成品。

同日用独立 D-Bus/XDG 目录启动 GTK4 GNOME Text Editor（GTK 4.22.4，强制 XWayland），确认实验 `qingjian` addon 加载；该夹具没有取得可重复的 InputContext 握手，故只记录为插件探测，不能标记应用自绘通过。完整矩阵和原始性能数据见 [Linux 自绘候选面板支持矩阵](linux-ui-support.md) 与 [linux-ui-timing.csv](linux-ui-timing.csv)。

2026-09-15 本机安装验证：在 Ubuntu 26.04 / Fcitx5 5.1.19 的现有 GNOME Wayland 会话中安装 release、启用用户服务并注册青简。首次实际加载发现用户 lib 不在 addon 搜索路径，已修正安装脚本；修正后通过运行中 Fcitx5 的 D-Bus 输入上下文确认插件加载、拼音预编辑及 `nihao` 上屏“你好”。这是实际守护进程链路验证，尚不代表各桌面应用的输入验收通过。

已提供 Router 输入/分页/隔离/持久化测试、真 socket 连接隔离与重启测试、Fcitx 实际 InputContext 的无桌面集成测试（preedit UTF-8 光标、译词 annotation、鼠标选词、密码旁路、断线放行）。新增初始化 Fcitx 事件链的 FocusOut 回归：无客户端 preedit 由插件提交，Preedit 由框架提交，Preedit + ClientUnfocusCommit 由客户端提交，三路均恰好上屏一次。另覆盖 Shift/停用遇到 Sensitive、Password、Disable 和未知能力时的协议顺序，以及私密缓存不会跨边界写入日志或学习文件。这些测试不能替代桌面前端验收。

仍需在 GTK、Qt、Electron、GNOME Terminal、Wayland 原生终端和 XWayland 应用验证 `nihao`、`kaifa`、`jintiantianqihenhao`、`nihooma`，记录失焦/输入法切换是否重复提交、密码框的能力标志、延迟、缩放主题和 Fcitx/Server 重启后的表现。
