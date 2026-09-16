# 固定测试字体

本目录仅供自动化测试与离线预览使用，运行时 `Renderer::new()` 使用系统字体。字体均采用 SIL Open Font License 1.1，完整许可与版权保留在 `OFL-Noto*.txt`（原 Debian 包版权文件，含字体适用许可）。CJK 与 emoji 子集已改名为 QingjianFixture，不能当成产品完整字体安装。

| 文件 | 来源 | 处理 |
|---|---|---|
| `NotoSans-Regular.ttf` | Noto Sans，Debian/Ubuntu `fonts-noto-core` | 原文件；拉丁字母、组合音标与连字测试 |
| `QingjianFixtureCJK.otf` | Noto Sans CJK Regular TTC，`fonts-noto-cjk`，SC face index 2 | 保留 `characters.txt` 字符、原整形关系与许可；重命名的测试子集 |
| `QingjianFixtureEmoji.ttf` | Noto Color Emoji，`fonts-noto-color-emoji` | 保留样例的 ZWJ / 彩色 emoji 及其关联 glyph；重命名测试子集 |

原始项目：[Noto Sans](https://github.com/notofonts/latin-greek-cyrillic)、[Noto CJK](https://github.com/notofonts/noto-cjk)、[Noto Emoji](https://github.com/googlefonts/noto-emoji)。字体原始 SHA-256：

```text
89c3c497f618fdaa0b2d1e98fef93582f28c71debd2c4a8cdf41f190ced2909d  NotoSans-Regular.ttf
b76b0433203017ca80401b2ee0dd69350349871c4b19d504c34dbdd80541690a  NotoSansCJK-Regular.ttc
9fd0a3d0ce84d77e3185dfbae77bd1abf3926aa49a032e354d076c4f17151f10  NotoColorEmoji.ttf
```

重建只需 Python 3 与 `fonttools==4.59.0`，对上述原文件执行：

```bash
python3 crates/qingjian-render/tests/fonts/subset.py \
  --cjk /path/to/NotoSansCJK-Regular.ttc \
  --emoji /path/to/NotoColorEmoji.ttf
```

脚本固定字符清单、TTC face、子集选项与原始时间戳，不探测本机字体也不联网。改样例时同步字符清单后再生成；不要删除 emoji 的 ZWJ、变体选择符或字体布局表。`characters.txt` 是固定测试字符集合，绝不读取用户输入。生成文件校验见 `SHA256SUMS`。
