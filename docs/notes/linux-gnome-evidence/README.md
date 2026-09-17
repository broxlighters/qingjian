# 2026-09-17 GNOME 阶段 0 证据

当前结论见[实施记录](../linux-gnome-stage0.md)。本目录保留两轮实验，**v2来源凭证已替换第一轮program/wmclass匹配**。所有Shell输入样本来自私有headless会话，不代表物理桌面、完整应用矩阵或发行沙箱通过。环境记录中的HEAD是实施前起点，当前未提交代码以`code-snapshot.json`逐文件SHA256标识。

## 第二轮 v2 身份、尺寸、恢复

- `v2-root-scenes.json`：root整理的17个固定输入场景，含GTK/Qt身份、多框、100%/150%/约167%、文字/UI倍率、默认恢复及**修复前跨屏失败**。仅保留固定测试文本、几何和monitor状态，不收录用户输入日志。
- `v2-followup-scenes.json`：正式夹具迁移后与reviewer独立复测。覆盖GTK/Qt多框多窗、双向167%→100%→167%无新键重绘、动态portal文字1→1.25、原装默认恢复再接管、补丁版默认定位再接管。每场保存复制进夹具的addon/扩展JS哈希，避免将后续源码快照倒推为历史二进制。
- `kimpanel-{coder,reviewer}-build.json`：两次从固定干净Fcitx5.1.19归档构建独立Kimpanel addon的命令、归档/patch/config/binary哈希。上游GNOME扩展来源commit为`b1e7718f445666cbe4e19ead8422f316e8cb3e39`，第三方源码未纳入仓库。
- `ctest-final-candidate.log`：无同时编译/桌面夹具的实验构建38/38通过，5.59s（在下述测试预热改动之前；产品代码语义此后未改）；`ctest-gnome-recheck.log`定向7/7通过，2.90s。
- `ctest-load-failure.txt`：此前并发重任务时4/38失败的工具输出摘录，原LastTest日志已被后续测试覆盖。没有静默删去失败，也没有放宽生产250ms。
- `ctest-gnome-warm.log`：随后只改传输测试的预热：计时前渲染固定字形，7/7通过，2.95s；生产代码截止时间不变；最后工作树的该测试文件应对应这份定向记录，不能说38项日志使用了预热后的文件。

原装Fcitx Kimpanel在禁用青简后默认面板可见可点击，但第一项落于`[13,71,...]`，不算恢复定位通过。显式`--fcitx-kimpanel`加载补丁后第一项`[375,397,...]`，既有相对光标恢复；禁用→默认mouse→重新启用→新输入自绘mouse精确“你好你好”。**系统原包仍未修，补丁不进入产品依赖。**

早期身份默认100%场景有部分使用尺寸字段加入前的Server构建，默认尺寸行为不受影响；text/ui测试使用重新构建的release Server。该差异已在聚合记录中明确，不以当前Server哈希冒充历史版本。

## 固定文本截图

`candidate-100.png`与`candidate-167.png`分别由root在单输出1280×720@100%、2880×1800@约167%的真实隔离Shell截图；`candidate-dual.png`来自官方Qt双向跨屏夹具返回167%后的截图。三份都只包含专门测试窗口和固定候选；原生Wayland，XCB关闭。前两份目视候选汉字、译文无裁切，但没有按参考图测量光标≤2逻辑像素误差。

Actor坐标/大小来自Shell **stage逻辑坐标**，PNG是Shell Screenshot输出像素；分数输出下不能直接把JSON坐标当PNG像素。PNG尺寸与倍率、SHA256见`v2-screenshots.json`。混合输出截图更不能统一乘一个倍率测量全部输出。`OUTPUT_MOVED.elapsed_ms`包含夹具固定400ms等待，只说明等待后状态正确，不作为重绘延迟。

## 当前复现入口

```sh
cargo build -p qingjian-render-ffi --locked
cargo build -p qingjian-linux-server --release --locked
cmake -S apps/linux/fcitx5 -B target/gnome-candidate-probe \
  -DQINGJIAN_RENDER_FFI=ON -DQINGJIAN_X11_BACKEND=OFF -DQINGJIAN_GNOME_PROBE=ON \
  -DQINGJIAN_RENDER_FFI_LIB="$PWD/target/debug/libqingjian_render_ffi.a"
cmake --build target/gnome-candidate-probe -j4
node apps/linux/probes/gnome/test.mjs
ctest --test-dir target/gnome-candidate-probe --output-on-failure
python3 apps/linux/probes/gnome/run-shell.py \
  --extension apps/linux/probes/gnome/extension --native gtk --mouse \
  --addon target/gnome-candidate-probe/qingjian.so --server target/release/qingjian-linux-server \
  --move-output --screenshot
```

其它选项见[探针README](../../../apps/linux/probes/gnome/README.md)：`--scenario fields|windows`、`--scale 100|150|167`、`--text-scale 1.25 [--no-follow-text]`、`--ui-scale 125|150`、`--fallback --kimpanel <原始源码>`。Qt需要系统可用PyQt6，或显式`--qt-pythonpath`；Firefox要求明示裸二进制路径，不将其等同Snap发行包。补丁构建/定位回归见[上游README](../../../apps/linux/upstream/fcitx5/README.md#kimpanel-恢复光标的独立实验补丁)。

## 第一轮历史样本与 XCB 调查

`bitmap-result.json`是无输入的v1密封memfd→GJS→St.ImageContent→after-paint→Hide实验，复现仍支持：

```sh
cmake -S apps/linux/probes/gnome -B target/gnome-probe
cmake --build target/gnome-probe
python3 apps/linux/probes/gnome/run-shell.py --extension apps/linux/probes/gnome/extension --sender target/gnome-probe/qingjian-gnome-probe
```

`gtk-candidate-*.json`、`gtk-mouse-reproducible.json`、`firefox-candidate-mouse.json`、`firefox-mouse-reproducible.json`是第一轮限定program窗口规则下的固定候选样本；`qt-input-baseline.json`当时只有键盘基线。本轮Qt身份/候选通过应引用v2聚合。旧`lease-*.json`及故障结果只证明撤窗，当时Fcitx `-u none`没有默认UI，不能据此宣称默认恢复。

`xcb-{hide,update}-1000.csv`是100次预热/1000次测量的汇总，**不是逐次样本**；仅short/vertical/scale1/highlight单场景，两个实验差每帧是否hide，均保留checked错误检测。后续`xcb-update-{debug,release}-raw.csv`保存逐次样本，对应`-summary.csv`汇总。debug p95/p99=7.045/7.601ms；最新Rust release archive+C++ Release为1.411/1.594ms。桌面均非受控，不能推广完整性能或启用auto。

```sh
cargo build -p qingjian-render-ffi --release --locked
cmake -S apps/linux/fcitx5 -B target/gnome-xcb-release \
  -DQINGJIAN_RENDER_FFI=ON -DQINGJIAN_X11_BACKEND=ON -DCMAKE_BUILD_TYPE=Release \
  -DQINGJIAN_RENDER_FFI_LIB="$PWD/target/release/libqingjian_render_ffi.a"
cmake --build target/gnome-xcb-release --target qingjian-xcb-timing
# :0必须为被测桌面的实际display；两个档位应固定字体、编译参数和负载。
target/gnome-xcb-release/qingjian-xcb-timing :0 --compare update target/xcb-update-raw.csv > target/xcb-update-summary.csv
```

`xcb-default-fallback.json`来自隔离Xvfb合成器退出后实际可见默认Fcitx面板mouse选词，精确“你好”且焦点不变。它不代表GNOME原生默认恢复；本轮原生结果见v2记录。缺系统xcb-randr开发包时可正常安装或传入隔离解包pkg-config路径，不能借此改变后端逻辑。
