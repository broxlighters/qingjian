# GNOME 候选位图协议

生产扩展 UUID 为 `qingjian@qingjian.local`，会话总线服务为 `org.qingjian.Panel1`，对象路径为 `/org/qingjian/Panel1`。协议 v1 只在同一用户会话内连接，并验证调用者 PID 与 `org.fcitx.Fcitx5` owner 的 PID 相同。实验探针继续使用独立 UUID 和 `PanelProbe1`，不参与生产选择。

所有可能超过 JavaScript 安全整数范围的身份字段都用十进制字符串传输：`generation`、`revision`、`transport_epoch`、`focus_epoch`、`submission`、`geometry_revision`。完整身份编码为 JSON；上下文是 32 位十六进制 UUID。扩展拒绝跨连接代次、旧提交、被 Hide 留下墓碑的提交以及旧几何。

## 生命周期

1. 插件每个 Fcitx 实例共享一个连接，提前调用 `Hello(1)`。扩展返回格式、上限、连接代次和 Shell 逻辑输出几何。
2. `BindContext` 携带完整身份和经验证的 D-Bus frontend 来源、同源按键、客户端相对光标及 scale。扩展用短命按键证明绑定当前 `Meta.Window`，返回目标输出 raster、可用像素大小和新几何代次。
3. `PrepareFrame` 传一个已写完并密封的 memfd、RGBA8 预乘位图参数、逻辑尺寸和交互区 JSON（最多130个有效候选/翻页矩形，以及可翻页方向和尺寸来源）。上限是 1600×900、5,760,000 字节；stride 固定为 `width * 4`。扩展异步读取，发 `Released` 代表不再读取 FD，发 `Prepared` 代表纹理已准备。
4. 插件在 `Prepared` 后调用 `Show`。扩展只在 Actor 参与 `after-paint` 后发 `Painted`；只有这个回执触发曝光记录，并把当前命中结果切换为可点击对象。
5. `Renew` 每 100 ms 续约，Fcitx 定时器明确使用 1 ms 精度，避免 systemd 默认合并把续约推迟至约 250 ms。租约为 500 ms，实际 `Painted` 也续展仍有效的窗口凭证。`Hide` 幂等地留下 submission 墓碑，撤销纹理、点击和窗口绑定并发 `Hidden`。连接、焦点、锁屏、Overview 或协议异常通过 `Lost(identity, reason)` 报告。

连续候选只保留当前可见帧、一个在途帧和一个最新待处理帧。更新时旧纹理保留到新 `Painted`，旧 revision 的点击在按键产生新帧时立即作废。Hide、失焦和断线优先于位图队列；晚到回执不能复活旧候选。

仅有效交互区的子 Actor 接收指针，阴影和空槽不建立命中层；非左键及无效翻页不会关闭当前帧交互。插件在首次有效动作前作废当前提交，重复信号不会再次选词。正常空帧直接 Idle/Hide，不进入位图故障回退。握手期间首帧进入有界 Preparing，重试定时器不被新按键绕过。

## 几何与故障

生产 v1 只接受已证明的 Fcitx D-Bus frontend `client-relative` 坐标。Shell 用窗口 buffer rect 转成 stage 坐标，目标 monitor 决定 raster，Actor 使用逻辑尺寸，因此不会再次缩放位图。输出、工作区或窗口变化会发 `GeometryChanged`，插件重新渲染并增加几何代次。

`OutputsChanged(s)` 直接传输当前全部逻辑输出矩形的 JSON 快照；它不改变传输 epoch，不触发重新 Hello，防止输出刷新与 Bind/Show 竞争。Hello 只跟踪连接 owner 代次，Bind/Hide 的提交代次不会取消同一连接协商。所有 D-Bus 错误名通过固定原因码白名单；未知 GJS 异常返回相应固定兜底原因，不把任意异常文本拼接成错误名。

固定原因码为 `extension_missing`、`protocol_mismatch`、`unsupported_frontend`、`anchor_unavailable`、`focus_mismatch`、`scale_unresolved`、`buffer_invalid`、`paint_timeout`、`transport_lost`、`renderer_unavailable` 和 `xcb_failure`。日志不得包含候选、拼音或位图。准备和绘制截止为 250 ms，隐藏租约上限 500 ms；失败后先撤窗或等租约失效，再恢复 Fcitx 面板。

Fcitx 5.1.19 的 IBus `ProcessKeyEvent(uuu)` 构造 `KeyEvent` 时 `time=0`。GNOME Shell 50 的 `Clutter.InputFocus` 也尚无已证明的 `Meta.Window` 绑定接口。生产 v1 因此不根据 PID、程序名或当前聚焦窗口猜测 IBus 身份；该 frontend 返回 `unsupported_frontend`，阻塞完整应用矩阵和 `auto` 默认开放。
