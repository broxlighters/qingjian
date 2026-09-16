# Wayland 候选 surface 的阶段 0 核验

2026-09-17，针对方案的阶段 0 决策门。本次核验没有通过 GNOME/KDE 色块验收，**不启用生产 Wayland 后端，也不声明双模式完成**。

## Fcitx 5.1.19 公开接口

核对固定版本源码，而不是从已安装协议 XML 推断能力：

- [`wayland_public.h`](https://github.com/fcitx/fcitx5/blob/5.1.19/src/modules/wayland/wayland_public.h) 提供连接创建/关闭回调，参数有 `wl_display *` 与 FocusGroup；[模块构建文件](https://github.com/fcitx/fcitx5/blob/5.1.19/src/modules/wayland/CMakeLists.txt) 导出安装该头文件。它**不是没有任何公开 Wayland API**：v1 可以在借用连接上用生成的 C 协议进一步研究。
- [`waylandim_public.h`](https://github.com/fcitx/fcitx5/blob/5.1.19/src/frontend/waylandim/waylandim_public.h) 的 `getInputMethodV2(InputContext *)` 返回 `fcitx::wayland::ZwpInputMethodV2 *`，不是原始 C 协议对象或独立 surface 句柄；[前端构建文件](https://github.com/fcitx/fcitx5/blob/5.1.19/src/frontend/waylandim/CMakeLists.txt) 未对外安装该模块接口，[包装库构建文件](https://github.com/fcitx/fcitx5/blob/5.1.19/src/lib/fcitx-wayland/input-method-v2/CMakeLists.txt) 为内部静态库和 BUILD_INTERFACE include，不能当作发行版已安装的公共 ABI。
- [Classic UI 的实现](https://github.com/fcitx/fcitx5/blob/5.1.19/src/ui/classic/waylandinputwindow.cpp) 分别按 `wayland` / `wayland_v2` frontend 创建 v1 overlay / v2 popup。青简没有复制其私有 Display、连接 user_data 或 wrapper 状态。
- [Fcitx 官方 Wayland 说明](https://fcitx-im.org/wiki/Using_Fcitx_5_on_Wayland/en) 区分 GNOME 的 IBus 通路和 KDE/input-method 通路。GNOME Shell 搜索还需要单独处理，不能由一个 `wayland:` 前缀证明 popup 可以创建。

## 可复现探针

`apps/linux/probes/wayland` 是独立程序，不编译进生产插件，不连接青简 Server，不读取用户输入，不绑定或抢占 input-method。只报告所连接 socket 对普通客户端开放的协议。`--surface` 在 v1 panel 可用时提交固定双色块，记录 frame callback、buffer release 和 pointer；不申请键盘、不创建 xdg_toplevel。

```bash
cmake -S apps/linux/probes/wayland -B target/wayland-probe
cmake --build target/wayland-probe --parallel 2
target/wayland-probe/qingjian-wayland-probe "$WAYLAND_DISPLAY"
target/wayland-probe/qingjian-wayland-probe "$WAYLAND_DISPLAY" --surface
```

依赖 `libwayland-dev wayland-protocols libfcitx5utils-dev`。使用 Fcitx EventLoop 处理 fd、sync callback、flush 的 EAGAIN 和 3 秒截止时间，没有阻塞 roundtrip。固定色块用 memfd + 明确 ARGB8888 格式，release 前不写回 buffer；退出先隐藏、销毁 surface，再关闭连接/映射。该单帧探针不是生产双缓冲、分数缩放或完整指针实现。

退出码：`0` 代表 registry 探测完成（色块模式还要求 frame callback）；`77` 表示连接/协议缺失或没有收到 frame callback；`1` 表示协议/I/O 失败，`2` 表示参数错误。frame callback **不等于光标定位、可见性或鼠标验收通过**。普通连接看不到的协议也可能只向 compositor 启动的输入法连接开放，必须在 KDE 的正式 Fcitx 连接上另测。

本机 GNOME Wayland 结果（普通 socket 连接）：

```text
compositor=1 shm_argb8888=1 input_panel_v1=0 input_method_v2=0
```

`--surface` 退出 77，没有创建替代顶层窗口。

| 场景 | 本次证据 | 结论 |
| --- | --- | --- |
| 本机 GNOME 普通 Wayland 连接 | registry 两轮异步 sync，v1/v2 不存在 | 无可用 input-panel，继续默认面板 |
| 本机 XWayland | 固定色块 CTest，实际像素/焦点/几何/鼠标队列通过 | 后端冒烟通过，仍不是应用矩阵验收 |
| KDE compositor 启动的 Fcitx 连接 | 当前环境没有该会话 | 未验证 |
| 其他 compositor | 当前环境没有该会话 | 未验证 |
| GNOME Shell / Kimpanel | 没有青简位图扩展承载 | 未实现，不宣称原生自绘 |

## 2026-09-17 本地 popup API 提案（未发送上游）

按方案 5.1 对固定 Fcitx 5.1.19 做了独立本地补丁，文件与复现步骤在 [`apps/linux/upstream/fcitx5`](../../apps/linux/upstream/fcitx5)。补丁只向 `waylandim` 安装 `waylandim_popup_public.h`，保留 Classic UI 的 build interface；青简生产插件没有链接该 API。`queryPopup`/`createPopup` 只接受实际激活、匹配、具有同连接 seat 和 v2 input-method manager 的 `wayland_v2` 上下文。v1、IBus、GNOME Shell、虚拟上下文错误父对象、能力受限、compositor 缺失均明确返回不可用。

Fcitx 在自己的连接中创建 surface 和 v2 role；消费者只借用原始 `wl_surface`/`wl_display`，不得覆盖 surface listener/user_data，也不得读 fd、dispatch、roundtrip、创建第二 reader 或断开连接。句柄失效先撤窗并清空 getter，再同步回调；回调重入可释放句柄，消费者负责销毁自己的 callback/buffer proxy 并释放映射。未收到 release 的存储不可覆写；销毁 proxy 后 compositor 仍持有其映射，消费者可关闭本地映射/FD。默认 `wayland:` 连接名合法。私有 libwayland-server socketpair 夹具、公开头独立消费者、六项生命周期测试及 ASan/UBSan 均通过；另有 patch SHA、双次 apply、reverse 和重复 apply 检查。

这只是可编译、可审查的上游提案，未发送外部上游，也未通过真实 GNOME/KDE popup 定位、缩放、鼠标和可见性门槛；Wayland backend 与 `auto` 仍关闭。

首版仅提供不可用原因、关闭和失效通知，不提供连接代次、pointer、输出/scale 或 rectangle 事件。句柄失效后永不复用；缓冲固定 scale=1。GNOME/KDE 色块、双缓冲和交互仍是下一阶段，不能把此提案当作阶段 0 全部通过。
