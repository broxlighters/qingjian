//! 按 Fcitx 输入上下文报告的 display 选择候选窗承载，并给出可诊断原因。
#pragma once
#include "kind.h"
#include <string>

namespace qingjian::panel {

struct BackendProbe final {
    DisplayKind display = DisplayKind::Unknown;

    /// 当前构建是否包含该后端；运行时合成器和屏幕能力还需建窗探测。
    bool available = false;

    /// 真实桌面和性能验收均通过时才允许 auto 选用。
    bool validated = false;

    /// 不含 display 名称和候选内容，可直接写入诊断日志。
    const char *reason = "未知输入上下文 display";
};

DisplayKind displayKind(const std::string &display);
BackendProbe probeBackend(const std::string &display);

}
