# 各 crate 的实现要点

CLAUDE.md 只保留目录地图与规则，每个 crate / app / tool 的实现细节收在这里：入口类型、数据文件、常数、生成命令。
改了实现要同步改这里；与代码冲突时以代码为准。

## crates/qingjian-dictionary

词库（TSV 解析或 `.qj` mmap），键按字节序排好，查询逐音节位置二分收窄（简拼位置按音节块跳扫），
`lookup_pattern`（≥ 模式长度）与 `lookup_exact`（正好等长）同一套实现。词库键以 `v` 表示 ü，
TSV 解析、查询与生成工具把 `lue` / `nue` 统一成 `lve` / `nve`。
旧 `.qj` 含这些键时，加载器建立规范化的内存词库。新 `.qj` 继续使用 mmap。

## crates/qingjian-core

模块：`composition` / `parser` / `correction`（拼写纠错：整段一处编辑的候选纠正 + `typo` 音节级敲错变体表，后者进整句词图当带代价的边）/
`candidate` / `ranking` / `shortcut` / `sentence` / `fuzzy` / `shuangpin`（双拼：四套方案键位表、键 → 全拼解码与消耗换算）/ `zhuyin`（大千注音：键 → 注音符号 → 拼音，`[general] zhuyin` 开关，声调只判音节完整不进查询）/ `emoji` /
`english`（英文模式候选）/ `engine`（`query::EnglishTail`：句末英文词并入整句，`woxiangxuehaorust` → 我想学好rust，尾段也像拼音时按分数与拼音读法比）。
`Engine` 是对外唯一门面，`Translator` / `Learner` trait 在 `engine` 模块；词库是「主词库 + 附加词库（`set_extra_dictionaries`）+ 用户词」的列表；繁体输出（`traditional` 开关与 `traditional_map` 映射）依赖 `ferrous-opencc`（`s2tw`）在出候选与上屏边界转换，内部保持简体。
`Engine` 是对外唯一门面，`Translator` / `Learner` trait 在 `engine` 模块；词库是「主词库 + 附加词库（`set_extra_dictionaries`）+ 用户词」的列表。
- 中英混输的英文词位置：`Engine::set_chinese_first`（配置 `[general] chinese_first`，缺省关）关着时拼音不像话的输入英文排第一（`extras::insert_english`，
  用户老选中文词时仍让中文在前），开着时整句先插、英文词紧随其后排第二（`query_inner` 里两步的先后按开关掉转）；句末英文词并入整句（`EnglishTail`）不受它影响。
  缺省关是回放定的（9241 词 / 269 条英文上屏：缺省开英文首选 82.5% → 7.1%）。
- `custom_phrase::merge_replacements` 把平台给的「输入码 → 短语」表（macOS 系统文本替换）并进配置里的自定义短语：每条占该码最靠前的空位（1–9），
  输入码不是小写字母、已有同码同文本、九位都满的跳过；Core 不管数据从哪来。
- 拼写纠错（`correction`）：整段一处编辑的变体里相邻换位允许末尾音节没敲完（`loose_segmentation`，`mignt` → `ming t…`；
  有「每个音节都完整、末尾不是落单单字母」的变体时残尾变体不参与，免得 `keyyi` 的 可以 被 `ke yi y…` 抢走），
  纠正之间比较时换位减 `TypoCosts::correction_transpose_discount`（1 nat，CLI `--tune correction-transpose=`），与原样比不减；
  2026-09-15 回放 283 词 / 23 句：词首选 89.0%、整句 82.6%，与改前持平（折扣若也用在与原样比，`zhongwne` 会误纠成 中文，整句掉一条）。

`EngineSession` 保存可挂起的组句、标点、历史与学习链，`Engine::swap_session` 在同一个引擎里交换输入状态，共用词库与落盘服务。切换上下文时清除查询及异步预测缓存，并由平台恢复各自私密状态。

`Engine::discard_input` / `EngineSession::discard_input` 用于隐私能力变化时无痕清理输入，包括透传缓冲、学习链和暂存词汇曝光；`set_private` 只切换写入开关，保留已输入的组句。

## crates/qingjian-translate

`Glossary`，本地 TSV 释义表（词性 + 译文）；`LevelTable`，词汇等级表（`assets/levels/levels-{en,ja}.tsv`，CEFR A1–C2 / JLPT N5–N1，
`uv run tools/corpus/levels.py` 从 `data/levels/` 的原始 CSV 生成，来源与许可见 `assets/levels/README.md`），「统计」页按级数词汇用，不进候选。

## crates/qingjian-learning

- `FrequencyLearner`：用户选择次数（`user.tsv`）、按输入串记的选择（`user-choices.tsv`，词级排序里同输入串选过的优先）、用户词（`user-words.tsv`，主词库同格式，
  Engine 与主词库一起查）、个人英文词（`user-english.tsv`，回车原样上屏的英文词与选过的英文候选，与随包英文词表一起出候选且在前）、
  个人敲错表（`user-typos.tsv`，接受过的 (敲的, 要的) 音节对，词图敲错边与整段纠错的代价按它打折）与个人 n-gram（`user-ngram.tsv`，Core `sentence::UserNgram`，
  二元 + 三元在线计数，整句转换与词级排序里与静态模型插值；Tab 接受的云端整句按 `sentence::segment_text` 切词后也记；
  连着选出的两个词记够次数自动造词进用户词，一段拼音分几次选完的合成词记两次也造）。
- `InputLog`：输入日志（`input-log.jsonl`，每次上屏一行：敲的键、切分、看到的前几个候选、选了第几个、来源、纠错、撤销，
  Core `InputLogger` trait 的落盘实现，`[general] input_log` 缺省开，只写本机，给离线回归评测与个人模型用）。
- `UsageStats`：输入统计（`usage.tsv`，按天记汉字 / 中文词 / 英文词 / 上屏次数，Core `UsageMeter` trait 的实现，Engine 每次上屏 `Usage::of_text` + 按来源定词数，
  整句按 `segment_text` 切词数；与输入日志无关，偏好设置「统计」页显示，`book_scale` 折成几本《某书》）。
- `VocabularyBook`：词汇记录（`user-vocab.tsv`，Core `VocabularyTracker` trait 的实现：学习语言的每条译词看到过几轮 / 上屏过 / ⌥+数字 打出过几次；Core 私密输入统一跳过曝光和提交写入，但仍可读取已有记录用于排序和生词标记；
  Engine `annotate` 据此填 `Sense::fresh`，看到轮次不到 `FRESH_UNTIL` = 3 的译词壳里画橙色；「看到」按上屏那一刻屏幕上那一页算，壳每次画完 `Engine::note_displayed` 告知当前页）。
- 各表落盘走 Core `storage::write_atomic`（临时文件 + fsync + 改名），加载按行容错（坏行警告跳过，真读不了壳退回内存学习），
  壳激活期间每 60 秒 `Engine::flush_learning`；IMK 回调边界 `imk::catch_panic` 拦 panic、缓冲区字母原样上屏（见 architecture.md「崩溃不丢」）。

## crates/qingjian-predict

- `CloudPredictor`：`Predictor` trait 的网络实现（async-openai，OpenAI 兼容接口，默认 DeepSeek），后台线程防抖 / 缓存 / 超时，`submit` / `poll` 非阻塞。
  `PredictConfig` 是配置的 `[predict]` 分节。只在组句中联想，一次请求给云端词（容错校验后补进候选第一页末尾 `[predict] slots` 格，缺省 2，不预留不占位，
  前面的本地候选不挪；排布在 Core `CandidateLayout`）和整句补全（preedit 右侧，Tab）；上屏后不联想，本地历史不进请求。
- `CloudGlossFiller`：释义兜底（Core `GlossFiller` trait，与 Predictor 分开的线程与通道，攒 1.5 秒 / 8 个词发一次，问过不再问）：
  随包释义表没有的词库词 / 云端词上屏后入队，结果壳每秒 `Engine::poll_glosses` 经 `Translator::learn` 写进 `qingjian-translate::PersonalGlossary`
  （`user-glossary-<语言>.tsv`，`LayeredTranslator` 个人表优先）；随云联想开关一起开。
- 问字键（缺省 `u`）开头是问字模式（`PredictionKind::Question`，答案带读音、不校验拼音），`?` 开头要 `ModeKeys::question_mark` 开着才算（配置 `[shortcut] question_mark`，缺省关，壳用 `Engine::takes_question_mark` 决定空缓冲区的 `?` 是入口还是标点）；`PredictionKind::Translate` 是壳里快捷键触发的「翻译选中文字」
  （双向：汉字为主译成学习语言，外文译回中文，`prediction::translation_target`），译文走结果的 `sentence`。

## crates/qingjian-format

`.qj` 数据容器（`Container` mmap 读、`Writer` 写、`Table<T>` / `Text` 零拷贝视图、`hash` 可落盘哈希索引、`Metadata` 名称 / 许可证 / 署名）。
词库与语言模型都能 `write_qj` / 从 `.qj` 打开，启动 50 ms；`cargo run --release -p qingjian-dict-convert -- pack dict|lm --name … --license …`
生成 `data/generated/{dict,lm}.qj`，`bundle.sh` 在 TSV 更新时自动重打并只把 `.qj` 打进包。设计见 `docs/design/architecture.md`「数据文件：`.qj` 容器」。

## crates/qingjian-neural

`CharScorer`，Core `sentence::SentenceScorer` trait 的实现：candle 加载字级 Transformer（GPT-2 风格 decoder，训练仓库（本地 `../train`，私有，不在本仓库）导出的
`model.safetensors` + `config.json` + `vocab.json`），给「前文 + 整句」按字累加 log 概率；前文的每层 K / V 缓存（`PrefixCache`），
同一段前文只算一次，每个候选只算自己那几个字（64 字前文 × 8 条 28 ms，Metal）。features `accelerate` / `metal` 换后端，壳用 `metal`。

Engine 侧在 `engine/rescoring/`：接了打分器就取 Viterbi 前 `RESCORE_PATHS` = 6 条路径按 `路径分 + λ·(神经分 − 静态二元分)` 重排（λ `NEURAL_WEIGHT` 0.5，
个人 n-gram / 用户加分 / 代价不动），分走「前文 + 文本 → 神经分」缓存 `NeuralCache`；同步打分器（`with_sentence_scorer`，CLI 评测）当场补分，
异步的（`with_async_sentence_scorer`，后台线程 `RescoreWorker`）查询不等模型：缺分的记下来，壳停键后 `request_rescoring`、`poll_rescoring` 到了再 `query` 一次。
前文优先用壳给的应用光标前文（`set_rescoring_context`），没有用本会话最近 64 个上屏字符。CLI `--neural <导出目录>`（`--neural-weight` / `--neural-context` / `--neural-async`）。

## crates/qingjian-lm

`BigramModel`，Core `sentence::LanguageModel` trait 的实现，从 `data/generated/lm.qj`（或 `lm-unigram.tsv` / `lm-bigram.tsv`）加载
（没有这两个文件就退化为一元词频整句）。数据由 `tools/corpus/parquet_to_text.py`（uv 脚本，HF parquet → 简体纯文本）加
`cargo run --release -p qingjian-dict-convert -- bigram --phrases assets/lexicon/phrases.tsv --phrases assets/lexicon/domain_words.tsv --brand assets/lexicon/brand.tsv --brand assets/lexicon/mixed_words.tsv data/corpus/*.txt` 生成；语料在 `data/corpus/`（gitignore）。
短语层不当 token 统计（分词时摘掉、统计完按成分合成一元 / 二元，短语得分等于原来两个词的路径，见 `bigram.rs` 模块注释），品牌词按给定次数写进一元与句首二元。

## crates/qingjian-platform

`Config`（TOML 配置文件，`[general]` / `[shortcut]` / `[fuzzy]` / `[dictionaries]` / `[apps]` / `[predict]` 分节，首次运行写模板，
`set_value` 用 toml_edit 原地改键保留注释；`[model] enabled` 本地整句模型开关，`LocalModelConfig`）；`extra_dictionaries` 列出 / 加载随包领域词库与用户 `dicts/`
（mac 壳与 Windows Server 共用，同名 `.qj` 优先于 `.tsv`）；`protocol` 模块是 Windows Server ↔ TSF DLL 的 IPC 协议类型
（`ClientMessage` / `ServerMessage` / `Frame` / `PreeditSegment`，全 serde，两端共用，见 `docs/design/architecture.md`「Windows：TSF」）。

## crates/qingjian-render

Linux 尺寸扩展在 `render-ffi` 主题副本分别应用用户 UI 倍率 `u` 与文字倍率 `t`：字体/行高乘 `u×t`，主题间距/圆角乘 `u`，`Theme.decoration_scale` 让固定云朵、光标、间距及有界面板阴影同样乘 `u`；最后由独立栅格倍率 `r` 渲染。共享主题 `decoration_scale=1`，其他平台默认输出不变。`qj_renderer_render_sized` 通过独立 `SIZE_ABI_VERSION=1` 校验参数，原 `qj_renderer_render` ABI v1 仍固定 `u=t=1`。Linux `LinuxHello` v1 以可选 `size_version=1` 传配置，缺字段按默认处理；不改公共候选协议。

自绘渲染器：候选窗一帧 + 主题 → 预乘 RGBA 位图，tiny-skia 栅格 + cosmic-text 文字（fontdb 按平台清单只加载几个字体文件、不扫系统），
自己解析 `trak` 字距表、按主题 gamma 加深笔画；cosmic-text 打了 `opsz` 光学字号补丁（qingjian-team/cosmic-text 分支 `qingjian-opsz`，workspace `[patch.crates-io]` 钉 rev）。
`examples/preview.rs` 出 PNG 与真机截图并排比、`--measure` 与 AppKit 对宽度。mac 壳 `candidates/bitmap/` 贴位图，`[general] renderer = "system"` 切回 AppKit 绘制
（过渡期退路，偏好设置「候选窗口」页可选）；`[general] font` 是候选窗字族名（空为系统字体，`bitmap/font_files.rs` 用 CoreText 按字族名找文件只加载那几个，没装就回系统字体；
设置页 `preferences/font_picker/` 是搜索框 + 列表）。设计与验收见 `docs/design/rendering.md`。

`text/` 按文字、像素字号、行高和点字号复用测量/绘制的整形结果，最多 512 段、2 MiB；颜色、gamma、删除线不进入整形键，高亮或主题变化继续使用新颜色。光学字号变更清整形和栅格；glyph raster 缓存达到 4096 项或 16 MiB 后回收，背景仍限制 8 张/8 MiB，gamma 表最多 16 项。字形按裁限后的行混合，覆盖率到预乘颜色的表每段计算一次，避免每字形分配 gamma 位图。`Renderer::clear_text_cache()` 丢弃文字及布局缓冲；Linux 在 reset/隐私变化/断线时经新增 C ABI 清理，不改变已有 result 生命周期。`examples/timing.rs` 支持 `--fixed-fonts`，固定许可字体与系统字体分别测 1/1.25/1.5/2×；离线数字不包含上传和 compositor 可见时间。

## apps/cli

测试工具，`cargo run -p qingjian-cli -- kaifa`。

- `--predict` 强制开云联想并等结果打印，交互模式下上屏后也联想。
- `--typing` 逐键计时（性能测试用 release 构建跑，目标每键 10 ms 以内）。
- `--chinese-first` 打开中文优先（`[general] chinese_first = true` 的排法），配合 `--replay` 比两种英文词位置。
- `--replay <input-log.jsonl>` 回放评测：把日志里每次上屏的键重新喂给引擎，按来源算首选 / 前五命中率、平均名次、不在候选的条数，打印没命中的例子（`--misses N`）；
  只在内存里学习不写文件，加 `--user-dict` 可带上现有学习数据。
- `--tune 名=值`（逗号分隔）覆盖个人 n-gram 插值与敲错代价的常数扫网格（名字见 `apps/cli/src/tuning.rs`，Core 侧是 `Engine::set_interpolation` / `set_typo_costs`，壳只用缺省值）。
- `--eval-text <文本>...` 整句评测：把用户自己写的中文文本按标点切句、按词库读音转成全拼，冷启动喂给引擎看整句能不能还原原句
  （首选命中率 / 字准确率 / 查询耗时；不依赖日志里当时选了什么，给整句排序与语言模型的改动当尺子），`--eval-save` 冻结成 `句子\t拼音\t上文` 三列文件，
  之后直接 `--eval-text` 它保证比的是同一份句子（本机的在 `data/eval/sentences.tsv`）。排序、整句、纠错的改动先跑它们再合。

## apps/macos

IMK 输入法，源码按 `app / host / imk / candidates / menubar / preferences` 分目录。

- 输入法菜单（状态项 + 系统输入源菜单）与偏好设置窗口都是配置文件的前端：只写 `config.toml`，`Host::apply_config` 一条通路热加载，激活期间每秒看一次文件 mtime。
- `apps/macos/scripts/bundle.sh --install` 打包安装到 `~/Library/Input Methods/`（开发用），`--pkg` 做分发用的 pkg（装 `/Library/Input Methods/`，postinstall 跑 `qingjian-macos --register`
  注册、启用并切成当前输入源；签名 / 公证靠 `QINGJIAN_SIGN_IDENTITY` / `QINGJIAN_INSTALLER_IDENTITY` / `QINGJIAN_NOTARY_PROFILE`，没设就 ad-hoc；`QINGJIAN_TARGET` 指定架构，
  成品 `target/pkg/qingjian-<版本>-macos-<arm64|x86_64>.pkg`）；`scripts/uninstall.sh` 卸载。
- 日志在 `~/Library/Logs/Qingjian/`（按天分文件留 7 天，删了会重建），用户数据与配置在 `~/Library/Application Support/Qingjian/`。
- 配置项：云联想 `[predict]`（偏好设置「云服务」页有「测试连接」按钮：`qingjian_predict::ConnectionTest` 起线程发一条最小请求，`Host` 用独立定时器 `CloudTestMonitor` 轮询结果显示到窗口底部；
  `reasoning_effort` 缺省 `none`，DeepSeek V4 默认思考，不关正文为空）；模糊音 `[fuzzy]` 默认都关；`[general]` 学习语言（`off` 不显示译文）/ 每页候选数 / 翻页键 / 外观 / 竖排横排 / 拼音显示位置 /
  英文模式候选开关 / 中文优先 `chinese_first` / 双拼方案 `shuangpin`（小鹤 / 自然码 / 微软 / 搜狗 / 小浪，空为全拼）/ 日志级别 `log_level`（缺省 info 不含敲的内容，debug 逐键记，热切换）/ 输入日志 `input_log`；
  `[shortcut]` 模式键 v / u、`question_mark`（缺省关，开了空缓冲区敲 `?` 进问字）、上屏第一 / 第二个译词的修饰键 `translation` / `translation_second`、删候选 `delete_candidate`（缺省 shift，用户词整删、词库词清学习）、翻译选中文字 `translate_selection`；
  `[apps] english_candidates_off` 按 bundle identifier 列出英文模式不给候选的应用（缺省终端 / 编辑器 / IDE，`*` 前缀匹配）；
  `[dictionaries] domains` 打开随包的领域词库（`Resources/dicts/` 11 本，缺省只开 `idioms`），`disabled` 关掉用户目录 `dicts/` 里的某本导入词库；
  偏好设置「词库」页随包的可开关、导入的可开关 / 移除，可导入 TSV / Rime yaml / .qj。
- 系统文本替换（系统设置「键盘 → 文本替换」）：`host/config/text_replacements.rs` 从 `NSUserDefaults` 全局域读 `NSUserDictionaryReplacementItems`
  （每条 `{ on, replace, with }`），激活输入法时重读，变了就经 Core `merge_replacements` 并进配置里的自定义短语再 `set_custom_phrases`；
  `[general] system_text_replacements` 开关（缺省开，「自定义短语」页勾选框），内容可能含证件号、地址，日志只记条数。
- 输入法进程由 launchd 拉起，看不到 shell 的环境变量：密钥写进配置同目录的 `.env`（`QINGJIAN_API_KEY=...`，输入法启动时 dotenvy 读入）或 `config.toml` 的 `api_key`。
- 本地整句模型：`bundle.sh` 把 `data/model/`（或 `QINGJIAN_MODEL_DIR`）三件套打进 `Resources/model/`，用户目录 `model/` 优先；`host/model/mod.rs` 在后台线程加载并预热（首次 Metal 编译）后
  `set_async_sentence_scorer` 接上，`refresh` 每键先读应用光标前 64 字给 Engine 当前文、查询后 `schedule_rescoring`，`RescoreMonitor` 停键 80 ms 请求、20 ms 轮询，
  结果到了重查一次只重画当前页（翻过页 / 动过高亮不动）；「云服务」页有开关（`[model] enabled`）。
- 端到端验证可用 `osascript` 的 System Events 往 TextEdit 发按键再读回文本（终端需要辅助功能权限；输入法得在中文模式）。

## apps/windows

一个产品两个 package：`server`（Server 进程：IPC 分派 + Engine + 命名管道 + 自绘候选窗与悬浮状态条）与 `tsf`（TSF 文本服务 DLL，lib 名固定 `qingjian_tsf`），
外加 `settings`（WinUI 3 设置程序）与 `installer`（Inno Setup）。不合成一个 crate，因为 DLL 不能带 Engine 的依赖树，见 `apps/windows/README.md`；
协议类型在 `qingjian-platform::protocol`，设计见 `docs/design/architecture.md`「Windows：TSF」。

TSF 原有数字 / OEM 标点 / 空格键码按当前布局用 `ToUnicodeEx` 解析（bit 2 避免改变键盘状态），
仅接受单个非代理项 UTF-16 单元。字母、小键盘和 AltGr 处理不变，不保证组合音符输入。

词库导入（设置「词库」页）走 `qingjian-dictionary::import` 转成 `.qj`（空词库拒绝），多选批量、成功的从 `[dictionaries] disabled` 摘掉、页面显示每个文件的结果；
Server 每次轮询比对用户 `dicts\` 的路径 / mtime / 长度快照，配置没变也重载新增、同名更新与移除；配置解析失败时词库沿用上次有效的开关（#36）。

## assets

- `assets/sample/`：手写样例词库与释义表，不是产品数据。
- `assets/emoji/emoji-zh.tsv` / `emoji-en.tsv`：Unicode CLDR 中文 / 英文 annotations 转出的 emoji 表（Unicode License v3，可发布；中文词与英文词各配 emoji，两张表加载时合成一张），
  `cargo run --release -p qingjian-dict-convert -- --out-dir assets/emoji emoji --language zh data/cldr/annotations-zh.json data/cldr/annotationsDerived-zh.json`（en 同理）。
- 英文词表词频：`uv run tools/corpus/english_frequency.py data/generated/english.tsv -o data/generated/english-frequency.tsv`，再 `... english <词表> --frequency <那个文件>`。

## tools/gloss-gen

用 LLM 批量生成释义表：`cargo run --release -p qingjian-gloss-gen -- generate`（密钥读 `QINGJIAN_API_KEY`，结果 JSONL 在 `data/generated/`，不进 git、可续跑，`--limit 80` 试跑）
再 `... export`（写 `glossary-{en,ja}.tsv`，产品数据在 `assets/glossary/`，见那里的 README；格式 `词\t词性. 译词[|假名]`）。CLI 与 bundle.sh 用的就是这两个文件。

## tools/dict-convert

产品数据的生成工具，输出到 `data/generated/`（gitignore）。

- `lexicon`：从 `assets/lexicon/`（自建词库源：规范字 + 常用词 + THUOCL 领域词）加 Unihan 读音（`data/unihan/Unihan_Readings.txt`）、LLM 多音字标注（`gloss-gen pinyin`，
  结果 `data/generated/pinyin-llm.jsonl`，不进 git）、语料词频（`lm-unigram.tsv`）建基础词库 `dict.tsv`（8.7 万条），并把 THUOCL 领域词按语料次数 < 50 拆成
  `dicts/<领域>.tsv` + `.qj`（11 本、13 万条，`--domain-keep-min`），流程见 `assets/lexicon/QINGJIAN.md`；`--extra-words` 并入人工挑的领域词 `assets/lexicon/domain_words.tsv`。
- `english`：转 `assets/lexicon/05_english/00_all_words.tsv`；`cedict`：释义表备用来源。
- `bigram`：统计语料；`--phrases` 给短语层、`--brand` 给品牌词（`assets/lexicon/brand.tsv`，青简 210）与中英混杂词（`mixed_words.tsv`，C盘 / B站：合成计数要成分词在语料里，C 不是 token，只能直接给一元，次数对着同音竞争词定），领域词也走合成计数（语料里只有几十次的词当 token 统计会吸走成分词的二元证据）。
- `mine`：从语料挖词库没收的高频词并过滤（`oov_filter.rs`：虚词规则 + 相邻字对 PMI≥3，`--candidates` 只重过滤）。
- `phrases`：挖短语层（两遍扫语料：相邻两词、两段二元都够频的相邻三词，总次数与对话语料次数都 ≥ 2000 + 边界规则，读音由成分词拼出；我的 / 不知道 / 有没有 这类常用词表不收的组合，
  `assets/lexicon/phrases.tsv`；词库已并入过短语时重跑加 `--refresh`）。
- `pack dict|lm|glossary`：打 `.qj`（释义表也进容器）。

## Linux Server / Fcitx5

程序资源安装到 `share/qingjian/resources/`，与 XDG 用户学习目录分开；卸载只移除程序资源。Linux 候选帧保留固定短语之间的空槽位，Fcitx 将它们显示为不可选占位，导航跳过空槽位，数字与鼠标选择使用相同位置。`fcitx5/tests/desktop/` 提供独立 D-Bus/Xvfb/XDG 的 GTK4 输入夹具，覆盖键盘、鼠标及合成器缺失/退出后的默认面板回退；bus 禁用服务自动激活，回退需实际点击可见 ClassicUI，`regression.py` 包含禁用默认 UI 反例及外部缩放污染检查。复现命令见 [支持矩阵](linux-ui-support.md)。

用户安装脚本将插件的绝对路径写入 addon 配置的 `Library`，因为 Fcitx5 默认不会搜索 `~/.local/lib/fcitx5`。重新安装时按当前 prefix 重新生成该路径。

Fcitx 面板先由 `backend/probe` 分类上下文 display，再由 `backend/selector` 创建承载；不读取会话类型猜后端。`QINGJIAN_X11_BACKEND` 默认 ON，最小安装显式 OFF（脚本 `--disable-x11`），旧实验缓存只迁移一次且新开关优先。`auto` 的真实验收表目前为空，默认仍 `fcitx`。X11 位图上限 1600×900，同尺寸复用 pixmap/BGRA 缓冲，仅上传首尾变化行包围区域，失败后完整重传；合成器 selection 每 250 ms 发起查询，通过 fd/定时器非阻塞轮询 reply，超过 1 秒无响应即回退，连接/合成器故障先隐藏并解除回调、再刷新默认面板，下帧重建失败连接；RandR/工作区改变则作废旧命中，通过 Engine 当前有效帧自动重绘。XWayland 几何变更有界等待 Shell/RandR 联合解析，不等待新按键。本地 submission 防止同一 Server identity 的重绘接收旧鼠标事件；事件派发持有后端共享寿命。阶段 0 Wayland 探针独立构建，结果与公开 API 结论见 [Wayland 承载核验](linux-wayland-api.md)。

`apps/linux/server` 使用独立产品版本 `0.1.0-dev`，Fcitx5 默认候选 UI。Core 的 `EngineSession` 仅保存输入状态，Router 按 SessionId 交换组句、历史、标点和学习链；词库、用户词频/用户词/个人 n-gram、统计与词汇记录共用进程内唯一实例，避免多个会话覆盖同一个文件。Unix socket 两端校验 UID，版本握手、连接编号重映射、断线回收、200 ms 客户端截止时间和候选帧版本检查都已接入。

正式 GNOME 展示使用 `qingjian@qingjian.local` / `org.qingjian.Panel1`，与阶段 0 探针完全分开。Fcitx 实例共享异步 `GnomeBridge`；`GnomePanel` 用 Preparing/AwaitingPaint/Visible/Hiding/Fallback 状态、完整十进制字符串身份、最新帧合并、250 ms 绘制截止和 500 ms 租约处理连续所有权。密封 memfd 上限 1600×900 RGBA8 预乘；`Released`、`Prepared`、`Painted` 分开，只有当前 `Painted` 切换命中结果并报告曝光。有效交互矩形由 `qj_result_region` 导出，Shell 只给有效候选/翻页区建立指针子 Actor；无动作输入不关闭交互，正常空帧直接隐藏。生产扩展按入口、协议、焦点、几何、Actor 和服务拆分，协议见 [GNOME 候选位图协议](../design/linux-gnome-panel-protocol.md)。

XWayland 只有在 X server 报告 XWAYLAND 且 RandR 全部输出与 Shell 全部逻辑输出能联合求出唯一 root→stage 比率时才使用该 raster；不从 Xft DPI 或单个客户端 scale 猜倍率。当前双屏证据得到统一 2:1；几何无法匹配时用 `scale_unresolved` 回退。原生 Shell 路径由目标 monitor 返回 raster，UI、文字和 raster 三种倍率仍分别应用。

输出快照通过 `OutputsChanged(s)` 更新，与 Hello/传输 epoch 分离；连接 owner 代次和 Bind/Hide 代次独立。续约100 ms定时器精度为1 ms，实际 Painted 续展有效窗口凭证，避免跨屏继承快到期的旧租约。正常 XCB 隐藏、失焦和空帧写入 Idle 诊断；提交失败仍保留 Fallback。

服务默认 unit 绑定 `graphical-session.target`，带启动限速与 15 秒停止期限；后台模式用安装器管理的 drop-in 和 `default.target` 链接，仍复用同一服务名/socket，要求用户预先明确启用 linger。`qingjian-session-setup` 迁移旧目标链接、保留停用/屏蔽及未知 override，`qingjian-diagnose` 只读报告 unit、socket、实际 Fcitx 映射、扩展 owner 和 Painted 状态。安装先暂存并逐文件 rename，不可变 generation 保存旧文件、manifest 和会话模式，发布失败恢复旧文件，多代清单供 `deploy.py --rollback`；自定义 prefix 与 `--no-start` 禁止会话操作。

主词库、领域词库、释义、emoji、英文词表、LM 与样例按 `AssemblySpec` 装配；Linux 不启用云/神经重排。XDG 配置/数据/日志路径、安装/卸载、协议和验证命令详见 [Linux Fcitx5 工程记录](linux-fcitx5.md)。桌面兼容矩阵仍待实测。

Linux 的 `Privacy` 实际变化会调用 Core `discard_input`，无痕丢弃该上下文的组句、透传缓存、历史、学习链与候选；挂起会话走 `EngineSession::discard_input`。`set_private` 仍只恢复写入开关，兼容 Windows 第一帧后报告隐私，普通/私密独立会话切换不丢组句。关闭挂起会话与进程退出均先按该会话隐私状态结束透传日志，再恢复其他上下文。Fcitx 的所有 Commit 入口统一刷新 Privacy；能力改变立即清面板，密码/Disable 不提交旧组句；FocusOut 只有服务端 preedit 时由插件提交，客户端 preedit 由 Fcitx 或 ClientUnfocusCommit 客户端处理。

### Fcitx popup API 本地提案

`apps/linux/upstream/fcitx5/` 保存固定 5.1.19 归档的最小 `waylandim` popup 公共 API patch、公开头消费者和 libwayland-server 生命周期夹具。它只支持激活匹配的 v2 context，Fcitx 独占连接 reader，surface/role 在同连接内创建；失效回调先撤窗再通知消费者销毁自有 buffer/callback。patch 可用 `check-patch.py` 重复应用校验，六项普通/ASan 测试通过。该提案未进入青简生产构建，真实 GNOME/KDE gate 未通过。


### GNOME窗口身份与尺寸实验

`QINGJIAN_GNOME_PROBE=ON`显式构建且运行时`QINGJIAN_GNOME_PROBE=1`才接入阶段0桥接，正常安装不启用。dbusfrontend的真实KeyEvent调用栈中，经公开`ObjectVTableBase::currentMessage()`复制已校验消息来源和IC UUID/epoch；Shell以窗口`notify::user-time`中的同源键事件证明窗口，PID仅作辅证。原型v2绑定总线daemon、sender/path、窗口对象及两端焦点代次；500ms/32条内存历史、单次签发墓碑，Withdraw保留有效proof，Hide撤销。无匹配事件的user-time校正不误当新键；IBus消息没有时间戳，仍默认回退。

Shell在monitor/workarea/raster变化时先撤纹理/点击再发GeometryChanged；插件用新submission重新读弱引用IC cursor/scale、Bind并栅格化，旧Painted/Pointer不复用。连续几何重试共用250ms截止，准备过程不续proof；有效显示每100ms最多一个Renew，lease500ms。仍为逐帧准备原型，不是正式连续所有权或默认启用条件。

Linux`ui_scale_percent`75..200和`follow_system_text_scale`经Server可选协商与独立sized ABI进入renderer。布局间距用UI倍率、文字另乘portal文字倍率、像素独立乘raster；其余平台旧ABI保持默认。Appearance在FFI/noX11构建也初始化。capability变更仅撤自己拥有的callback，对仍打开的青简会话重建默认帧；未打开或其它IME的回调不碰。

`fcitx5-5.1.19-kimpanel-resume.patch`是独立上游实验：Kimpanel恢复时在异步relative协议查询完成后刷新现有cursor，suspend取消查询；没有向产品壳添加假FocusIn或固定延迟。`build-kimpanel.py`校验归档/SDK5.1.19，在新目录编译独立addon；只能由隔离`--fcitx-kimpanel`夹具显式加载，不是发行依赖。实现和证据边界见[GNOME阶段0记录](linux-gnome-stage0.md)。
