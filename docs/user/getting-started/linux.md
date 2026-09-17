# 在 Linux 使用青简

Linux 版本正在进行兼容性验证。Debian/Ubuntu 用户可安装对应的 `.deb` 包；从源码安装和打包步骤见 [构建与安装说明](../../notes/linux-fcitx5.md)。首个目标环境为 Ubuntu 26.04 和 Fcitx5 5.1.x。

安装包下载到本地后运行：

```bash
sudo apt install ./qingjian-fcitx5_<版本>_<架构>.deb
```

安装后以桌面用户登记默认的图形会话启动模式和 GNOME 扩展，再重启 Fcitx5，在配置工具中添加「青简」：

```bash
qingjian-session-setup --enable --enable-extension
```

首次安装扩展后若系统提示 Shell 尚未发现它，请重新登录。安装过程不会自动注销、重启 Shell 或修改 Fcitx 输入法列表。服务默认随 GNOME 图形会话启动、登出停止；诊断可运行 `qingjian-diagnose`，它不会重启服务或读取输入历史。

需要登录前启动并在登出后继续运行 Server 时，先了解 systemd linger 会影响该用户的全部服务，再由用户自行开启 linger，然后运行 `qingjian-session-setup --startup=background --enable`。切回默认模式运行 `qingjian-session-setup --startup=session --enable`；青简不会自动关闭 linger。

仓库安装脚本默认安装到用户目录。安装后启动青简服务，重启 Fcitx5，在 Fcitx5 配置工具的输入法列表中添加「青简」。候选外观使用 Fcitx5 的主题，候选旁显示一条译词；「生」表示尚不熟悉的译词。

输入拼音后，空格选择当前候选，1–9 选择当前页候选。上/下键和 Tab 移动高亮，Shift+Tab 向前移动；PageUp/PageDown 翻页。左/右、Home/End 移动拼音光标，Backspace 删除前一个字母，Delete 删除后一个字母，回车原样输入，Esc 清空。单击 Shift 切换中英文，Caps Lock 临时输入大写英文。Alt+数字输入第一条译词，Alt+Shift+数字输入第二条译词；Shift+数字删除用户候选或清除对应学习记录。译词和删除快捷键可在配置文件中修改。

默认配置位于 `~/.config/qingjian/config.toml`。可设置双拼、模糊音、学习语言、中文标点、自定义短语和候选数量，修改后重启青简服务。Linux 暂无设置窗口；云联想、选区翻译和神经模型开关暂不生效。

候选窗口默认使用 Fcitx5 的外观。可在配置中设置候选方向和拼音位置：

```toml
[general]
layout = "vertical" # vertical 竖排 / horizontal 横排
preedit = "both"    # both 两处显示 / inline 应用内 / window 候选窗内

[linux_ui]
renderer = "fcitx"  # fcitx 默认外观 / auto 自动选择 / qingjian 优先请求青简外观
ui_scale_percent = 100 # 青简外观大小，75–200 的整数
follow_system_text_scale = true # 跟随系统文字大小
```

修改后重启青简服务和 Fcitx5。应用不支持行内拼音时，拼音会保留在候选窗内。默认外观中的横竖排、译词显示效果仍取决于应用和 Fcitx5 前端。

青简外观的大小可设为 `100`、`125`、`150` 等；小于 `75` 或大于 `200` 时采用 `100` 并记录提示。这一选项会重新排版文字与间距，只影响青简自绘外观。GNOME 的文字放大也会单独生效；关闭 `follow_system_text_scale` 可保持青简的文字大小偏好，显示器缩放仍然生效。桌面未提供文字大小时采用正常文字大小。Fcitx5 的字体设置不会自动改写青简设置。混合缩放下的显示尺寸仍待兼容性验收。

青简自绘外观仍在兼容性验证中。完整包包含 X11/XWayland 自绘窗口和 GNOME 50 配套扩展，设置 `qingjian` 后只在能可靠绑定输入框与窗口的通路尝试使用；窗口无法定位、桌面不支持或绘制失败时自动恢复 Fcitx5 面板。IBus、GNOME 搜索和未经验证的应用继续回退，`auto` 暂不启用自绘。出现显示问题时改回 `fcitx` 即可恢复默认外观。

实验外观支持浅深色、横竖排、多义项、日文读音和橙色生词；`theme = "system"` 跟随桌面外观，无法读取系统偏好时使用浅色，也可指定 `light` 或 `dark`。候选可点选，页码两侧的箭头及滚轮可翻页。隐藏或被截断的译词不会作为完整展示记录。默认面板无法确认应用实际绘制的范围，只按交给它的第一条译词估计曝光；这不等于测量用户看到了什么。

词频、用户词和词汇记录保存在 `~/.local/share/qingjian/`，日志在 `~/.local/state/qingjian/logs/`；设置了 XDG 路径时使用对应位置。密码框直接使用原始按键，应用标记的私密输入不学习、不记录输入日志，也不写入词汇记录。输入框进入或退出私密状态时，会清空尚未上屏的拼音和候选，避免把私密内容带到普通输入框。服务暂时不可用时按键会直接交给应用，服务恢复后的下一次按键会重连；未提交的拼音可能丢失。

卸载程序会保留配置和学习记录。卸载后在 Fcitx5 配置工具移除青简并重启 Fcitx5。
