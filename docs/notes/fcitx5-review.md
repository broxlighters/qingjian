# Fcitx5 实现复核记录

2026-09-15：经 coder 修复、reviewer 独立复审及追加修复，当前结论 **通过**。此前四项问题及复审新增的一项词汇隐私问题均已修复，未发现有证据的阻塞缺陷。此结论覆盖代码与下述自动化验证；GTK / Qt / Electron / Wayland 真正桌面验收仍未完成。

## 已修复问题

- **P1：退出私密模式泄露缓存输入。** 原先只切换私密标志，私密阶段的透传、未提交组句及学习链可能在恢复普通模式后落盘。现在 Linux 会话默认私密，隐私状态实际变化时调用 Core 的 `discard_input`，清理组句、透传、历史、学习链及暂存曝光；活动和挂起会话均覆盖。真实进程验证私密 `SECRET`、`nihao` 及“你好→开发”学习链不外泄，其他普通会话的组句仍可恢复。Core 的 `set_private` 保留原有语义，避免影响 Windows 首次组句和跨会话恢复。
- **P1：没有客户端 preedit 能力时失焦丢字。** 原先所有 FocusOut 都跳过提交。现在通过统一 `commitRaw` 清理服务端状态：客户端 preedit 已有内容时由框架或客户端处理上屏，否则由插件提交原始输入。覆盖候选窗 preedit、客户端 preedit、ClientUnfocusCommit 及随后 Reset，防止丢字或重复上屏。Fcitx 5.1.19 对 `clientPreedit` 的失焦处理见 [官方实现](https://github.com/fcitx/fcitx5/blob/5.1.19/src/lib/fcitx/instance.cpp#L1045-L1073)。
- **P1：Shift 与切换输入法的提交绕过隐私刷新。** 统一提交入口先同步当前能力，任一 Password / Sensitive 标志使用 `testAny` 判断；能力未知按私密处理。Password / Disable 清理并断开会话，能力变化导致的停用也先进入私密状态。测试覆盖 Shift、deactivate 和能力变化路径。
- **P2：关闭挂起会话丢失普通输入日志。** 关闭时恢复目标会话及其隐私状态，处理透传缓冲，再恢复当前会话；Router 析构逐一关闭剩余会话。真实连接验证普通 A / B 会话无论显式关闭还是 SIGTERM 退出都保留日志，私密缓冲不落盘。
- **P1：私密译词仍写入用户词汇记录。** reviewer 追加发现 `VocabularyBook` 的曝光与提交写入未受私密状态保护。Core 两个写入入口现均检查私密状态，并丢弃私密阶段的暂存曝光；只读译词及生词标记仍可用。Core 回归、真实 `VocabularyBook` + `Glossary` 测试及独立进程复现验证：私密中文选词、译词快捷键和原样提交不新增词条，不改变已有词条次数或日期；恢复普通输入后正常记录。

## 最终验证

- `cargo test --workspace --exclude qingjian-macos --locked`：398 项通过、0 项失败、1 项忽略。
- `cargo clippy --workspace --exclude qingjian-macos --all-targets --locked -- -D warnings`、`cargo fmt --all -- --check`、`git diff --check` 均通过。
- Fcitx5 插件编译与 12 项 CTest 通过，包含失焦、隐私能力变化及卸载保留用户数据。
- 独立 reviewer 对追加词汇修复重新检查全部生产词汇写入入口，Core 229 项、learning 18 项、Linux 13 项测试通过；重新构建 server 后，真实 Unix socket 的隐私边界、挂起会话、SIGTERM 和词汇落盘探针通过。

coder 所在受限执行环境曾禁止 Unix socket `bind`；主 agent 与最终 reviewer 已在允许本地 socket 的环境补跑并通过真实进程验证。上述检查不替代桌面应用兼容性验收。

## 此前实现与验证

主 agent 的补充检查发现并修正：

- 默认安装资源目录与 XDG 学习目录重合，卸载会删除用户词频。资源已迁入 `share/qingjian/resources/`，新增卸载保留 `user.tsv` 和自定义词库的回归测试。
- 稀疏自定义短语显示时压缩了空槽位，数字标签与实际选择位置不一致。帧现在保留不可选占位，导航跳过空槽位，增加跨页、数字选择和鼠标选择测试。
- 删除候选的诊断日志会包含候选文本。已移除这条日志，提示仍显示在输入界面。

已验证真实 Unix socket 的独立连接、组句隔离、普通与私密学习数据隔离、中文标点、Ctrl 放行、Esc 和回车；插件通过隔离 Fcitx Instance 的实际加载检查。测试命令及桌面验收范围见 [Linux 工程记录](linux-fcitx5.md)。

临时前缀安装支持路径中的空格，从仓库外启动能够定位资源，校验表通过，卸载保留原有用户词频。
