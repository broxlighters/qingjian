# Linux 双模式实现审查记录

2026-09-17。本轮按 coder 实现 → reviewer 审查 → coder 修复 → reviewer 复审推进。审查“通过”只评价交付代码及其证据，不等于 [双模式方案](../plan/linux_dual_ui_support_plan.md) 的真实桌面与性能门槛通过。

## X11 增量上传与健康检查

首轮结论：通过，无已知阻塞缺陷。

- 同尺寸 pixmap 复用、BGRA 差分行上传、失败后完整重传及尺寸上限一致。
- 非阻塞合成器 owner 查询、超时失效、Controller 回调重入及旧 submission 丢弃通过检查。
- 隐私清理、实际 display 路由、默认面板回退和 `auto` 的关闭状态符合方案边界。
- coder 与主代理验证最小构建 25 项、仅 FFI 26 项、FFI + X11 28 项 CTest；reviewer 复核核心子集及 render/render-ffi 测试。reviewer 首轮沙箱限制 socket，完整 socket/X11 结果由主任务的实际运行补足。

实际 XCB 48 场景数据仍未达到性能目标：[原始 CSV](linux-ui-timing-xcb.csv)。不能以实现审查通过放开 `auto`。

## GTK4 隔离输入夹具

首轮结论：不通过。reviewer 通过反例确认三项问题，已交 coder 修复：

1. D-Bus 在隔离环境建立前启动，可自动激活 portal、GVfs、keyring 等用户服务。
2. 禁用 Classic UI 后两个回退用例仍通过；上屏成功不足以证明默认候选面板恢复。
3. 外部 `GDK_SCALE=2` 会改变实际缩放，但结果仍硬编码记录 1×。

coder 已逐项修复：在启动总线前隔离 HOME/XDG/display，显式私有 D-Bus 配置不加载服务目录；检查 `Fcitx5 Input Window` 可见并实际点击首项上屏，同时检查自绘/默认窗口互斥与焦点；固定缩放并从 GTK 读取实际值。

第二轮 reviewer 独立复审结论：**通过**。`regression.py` 四个正向用例通过；两个禁用 Classic UI 的反例均因默认候选窗口缺失而失败且不生成通过结果。外部注入 `GDK_SCALE=2`、`GDK_DPI_SCALE=2` 后实际 GTK scale 为 1。六次私有总线只列出 D-Bus 自身可激活，清理记录及独立进程扫描没有夹具进程泄漏。

复审另提出的低严重性问题也已修复：`regression.py` 在回归循环前处理 `-h`/`--help`，不创建 `Artifacts:`，不启动 D-Bus、Xvfb、Fcitx 或 Server。reviewer 定向复审确认两种帮助参数均 exit 0、有 usage、无异常和临时产物。

## Fcitx popup API 独立补丁

独立补丁、公开头消费者、协议生命周期夹具与复现步骤位于 [`apps/linux/upstream/fcitx5`](../../apps/linux/upstream/fcitx5)。reviewer 最终审查结论：**通过**，无已知阻塞缺陷。

- 归档 SHA256、两份干净源码应用、反向检查、重复应用拒绝，以及 8 个文件与实际构建源码一致性检查通过。
- 真实 `waylandim`/`wayland` 构建、普通 CTest 6/6、ASan/UBSan CTest 6/6 通过；reviewer 独立复跑两套测试。
- 公共头不泄漏私有 wrapper；同连接 surface/role、关闭顺序、失焦/隐私/虚拟上下文/协议失效、同步重入、活跃 buffer/frame 的断线与卸载路径通过审查。

补丁仅作为本地独立上游提案，未发送上游、不链接进青简生产插件、不安装到系统，也不表示 GNOME/KDE 的 popup 定位、缩放、鼠标及可见性已经通过。阶段 0 真实桌面门槛及方案阶段 2/3 仍未完成。

本轮运行日志位于工作区 `target/dual-ui/`；最终行为、复现命令与发布边界以 [支持矩阵](linux-ui-support.md) 和 [Wayland API 核验](linux-wayland-api.md) 为准。
