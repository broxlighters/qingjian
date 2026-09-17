# Fcitx 5.1.19 独立本地提案

这是方案第 5.1 节的独立本地实现，已通过本轮 reviewer 审查，记录见 [审查记录](../../../../docs/notes/linux-dual-ui-review.md)。未发送上游、未发布 PR，未接入青简生产构建，也不代表 GNOME/KDE 或整套双模式验收通过。补丁新增代码采用上游 `LGPL-2.1-or-later`，不复制整个上游树或生成协议文件。

## API 边界

- `queryPopup(InputContext *)` 返回明确的可用性/失败原因；`createPopup(InputContext *, closed)` 返回 RAII `WaylandPopup` 句柄。只接受真实、匹配且激活的 `wayland_v2` 或它当前委托的虚拟上下文，检查实际 server、FocusGroup、seat、display 和能力。默认 `wayland:` 的空连接名合法；v1、IBus/GNOME Shell 不可用。
- Fcitx 内部创建同连接的 surface 与 v2 role。消费者只见安装后的 `waylandim_popup_public.h`、公开 Fcitx 头和原始 `wl_surface`/`wl_display`，不链接内部 wrapper ABI。旧 `waylandim_public.h` **不安装**，Classic UI 的源码 build interface 保持原状。
- Fcitx 拥有 surface/role 及其 listener/user_data。消费者不得销毁/覆盖这些对象，不得改变 event queue 或赋予其它 role。消费者自己创建的 frame callback、buffer、registry 等可拥有自己的 listener，但必须来自借用 display，并在关闭回调内销毁 proxy、清空缓存指针。已提交存储在 release 前不可覆写；销毁 proxy 后消费者可释放本地映射/FD，compositor 保留自己的映射。
- Fcitx 仍是唯一 reader，消费者不能 dispatch、prepare/read events、roundtrip、disconnect 或创建第二 fd reader；请求由 Fcitx 现有事件循环 flush。所有 API/回调在 Fcitx 主线程使用。
- 失焦、reset、capability 变化、输入法切换、v2 deactivate/连续 activate/unavailable、context 销毁、连接关闭、协议 global 移除和模块退出都撤窗。先销毁 role/surface 并清空 getter，再同步调用一次 `closed`；连接尚未 disconnect。回调可释放自己的句柄、关闭其它句柄，自动失效期间不能重新创建 popup。消费者不能在回调内销毁 Instance/InputContext、卸载 addon、抛异常或递归运行事件循环。
- `close()` 幂等并同步通知；直接析构句柄撤窗但不通知，消费者析构时须自己先清理 proxy 或显式 close。句柄不可跨 addon 卸载存活。v2 protocol version=1，首版固定 buffer scale=1；没有输出/scale/rectangle 通知、连接代次或 pointer 保证。不负责默认面板互斥。

## 应用与真实上游构建

固定完整归档 `target/dual-ui/fcitx5-5.1.19-complete.tar.gz`：

```text
SHA256 a6e4d99a82298845df9f8dc5036c1aaed258c2d3f5bd215b8f91c75410167ff0
```

以下命令从仓库根执行，新建独立源码/构建/安装目录，不改系统。现有已打补丁源码位于 `target/dual-ui/upstream/fcitx5-5.1.19`，不要向它重复 apply。

```bash
patch_file="$PWD/apps/linux/upstream/fcitx5/fcitx5-5.1.19-popup-api.patch"
task_root="$PWD/target/dual-ui/upstream/reproduce"
task_deps="$PWD/target/dual-ui/upstream/deps/root/usr"
mkdir -p "$task_root"
tar -xzf target/dual-ui/fcitx5-5.1.19-complete.tar.gz -C "$task_root"
task_source="$task_root/fcitx5-5.1.19"
task_build="$task_root/build"
task_prefix="$task_root/install"
git -C "$task_source" apply --check "$patch_file"
git -C "$task_source" apply "$patch_file"

PATH="$task_deps/bin:$PATH" cmake -S "$task_source" -B "$task_build" \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX="$task_prefix" \
  -DCMAKE_PREFIX_PATH="$task_deps" -DECM_DIR="$task_deps/share/ECM/cmake" \
  -DENABLE_X11=OFF -DENABLE_ENCHANT=OFF -DENABLE_EMOJI=OFF \
  -DBUILD_SPELL_DICT=OFF -DENABLE_TEST=OFF -DUSE_SYSTEM_YOGA=ON
PATH="$task_deps/bin:$PATH" cmake --build "$task_build" \
  --target waylandim wayland --parallel 3
cmake --install "$task_build" --component header
cmake -DCMAKE_INSTALL_COMPONENT=Unspecified \
  -P "$task_build/src/frontend/waylandim/cmake_install.cmake"
```

ECM/gettext/yoga 在 `target/dual-ui/upstream/deps/root` 临时解包；系统还需 C++20、CMake、pkg-config、Wayland 1.22+ 开发头、wayland-scanner、wayland-protocols 1.39+、xkbcommon 和 Fcitx 自身的常规依赖。测试额外需 `libwayland-server`。已有构建环境说明在 `target/dual-ui/upstream/README.local`。原 CLI workspace-write 沙箱曾使 `wl_client_create` 的 socket 查询返回 EPERM；需允许私有 socket 操作的环境，不能把 socketpair 改成 listening socket API 来掩盖限制。

## 验证

```bash
python3 apps/linux/upstream/fcitx5/check-patch.py \
  target/dual-ui/fcitx5-5.1.19-complete.tar.gz --built-source "$task_source"
cmake -S apps/linux/upstream/fcitx5/tests -B "$task_root/tests" \
  -DFCITX_SOURCE="$task_source" -DFCITX_BUILD="$task_build" \
  -DFCITX_PREFIX="$task_prefix"
cmake --build "$task_root/tests" --parallel 3
ctest --test-dir "$task_root/tests" --output-on-failure
```

`check-patch.py` 验证归档 SHA256，在两份干净源码 apply、reverse-check，确认重复 apply 被拒绝，并逐字比较 patch 结果与实际构建源码。`tests/CMakeLists.txt` 通过已安装 `Fcitx5ModuleWaylandIM` CMake 包编译独立消费者，禁止 SDK 误装旧 private 头。消费者只链接 Fcitx Core/Config/Utils 与 Wayland C client；生命周期夹具另编译内部 virtual-context 测试支持，不把内部头给消费者。

2026-09-17 实测：真实 `waylandim`/`wayland` 构建通过，六项 CTest 通过。夹具加载真正的两个 addon，经 `wl_client_create` 私有 socketpair + libwayland-server 检查同连接 surface/role 请求；Fcitx 的 WaylandEventReader 是唯一 client reader。覆盖：

| 用例 | 断言 |
| --- | --- |
| consumer | 安装头函数签名编译、链接；无私有 wrapper/生成 client 协议头 |
| lifecycle | 空/伪造 frontend/错误虚拟父对象拒绝；默认空 display、相同 display 名但不同 FocusGroup 拒绝；真实虚拟上下文成功；同连接 SHM 提交；listener user_data 已占用；关闭幂等、析构、失焦、四类受限能力、reset、连续 activate、unavailable 后不再可用 |
| disconnect | 活跃 popup、有未完成 frame callback/已提交 buffer 时断开 server；回调在 display 释放前销毁自建 proxy，可同步释放句柄 |
| unload | 模块退出先通知并清理活跃 popup 及自建对象 |
| global | compositor global 移除即撤窗、销毁 role/surface、查询变为缺少 compositor |
| missing | 从未提供 compositor 时拒绝创建，不发送 surface/role 请求 |

相同源码用 `-fsanitize=address,undefined -fno-omit-frame-pointer` 分别配置独立 `build-asan` 和 `popup-tests-asan`，上游两个 addon、Core/Config/Utils 和测试全部重新编译；`ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest ...` 六项通过。现有日志在 `target/dual-ui/upstream/{build-popup,test,test-asan}.log`。

这些测试不运行真实 GNOME/KDE compositor，不证明光标定位、鼠标、缩放、多屏、默认面板互斥或首帧可见时间。GNOME 没有该 input-method 协议时仍回退；此popup提案不含Shell扩展（另有显式启用的阶段0 GNOME原型）；KDE/其他 compositor 尚无实机记录。Wayland backend 与 `auto` 的生产门槛保持未通过。


## Kimpanel 恢复光标的独立实验补丁

`fcitx5-5.1.19-kimpanel-resume.patch`与上面的popup API互相独立，不要为此先应用popup patch。GNOME Kimpanel重载时，Fcitx `resume()`异步查询relative协议能力，却只订阅将来的光标变化；客户端相同矩形被去重，已有候选恢复到屏幕左上角。补丁在Introspect完成后用既有cursor方法刷新当前focused IC，查询期间不把relative位置当绝对屏幕位置，suspend取消查询。它不更改候选、提交、焦点或坐标算法，新增代码沿用上游LGPL-2.1-or-later。

只在私有夹具加载，没有安装系统、进入青简生产依赖或发送上游。构建脚本校验上述完整归档SHA256、系统SDK5.1.19和同版本上游CMake生成的config.h，在必须不存在的新目录解包/apply/reverse-check/拒绝重复apply，再编译一个链接系统Fcitx库的`libkimpanel.so`；归档、patch、config和二进制哈希保存在build.json。config-dir可使用前节正常configure后生成的目录，不需要应用popup patch。

```sh
python3 apps/linux/upstream/fcitx5/build-kimpanel.py \
  target/dual-ui/fcitx5-5.1.19-complete.tar.gz \
  --config-dir target/dual-ui/upstream/build \
  --output target/gnome-kimpanel-reproduce
cargo build -p qingjian-linux-server --release --locked
python3 apps/linux/probes/gnome/run-shell.py \
  --extension apps/linux/probes/gnome/extension --native gtk --mouse \
  --addon target/gnome-candidate-probe/qingjian.so \
  --server target/release/qingjian-linux-server \
  --fallback --kimpanel target/gnome-identity/kimpanel-src \
  --fcitx-kimpanel target/gnome-kimpanel-reproduce/libkimpanel.so
```

`--kimpanel`指明确提供的原始GNOME扩展源码，本轮commit `b1e7718f445666cbe4e19ead8422f316e8cb3e39`；只在私有副本加TestState并记录源文件哈希。`--fcitx-kimpanel`指本补丁生成的Fcitx addon，两者不同。移除后者即可复现原装Fcitx恢复位置错误；原装已有可见/点击/重新接管闭环，但不能算定位通过。带补丁时还断言relative spot与实际候选位置，最后恰好“你好你好”。

2026-09-17 coder/reviewer独立运行：修复前第一项`[13,71,...]`；修复后`[375,397,...]`（同场青简Actor原点`[368,465]`），各110.8/142.8ms内观察默认恢复，鼠标选词、重启青简扩展、新输入自绘选词均通过。该毫秒数包含轮询粒度，仅是两次隔离样本。原始结果摘要、构建hash见[GNOME证据](../../../../docs/notes/linux-gnome-evidence/README.md)，不替代物理桌面/完整故障验收。
