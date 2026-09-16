//! Linux 候选窗后端能力探针。
#include "probe.h"
#include <string_view>

namespace qingjian::panel {
namespace {
bool prefixed(std::string_view value, std::string_view prefix) {
    return value.size() > prefix.size() && value.substr(0, prefix.size()) == prefix;
}
}

DisplayKind displayKind(const std::string &display) {
    if (prefixed(display, "x11:")) return DisplayKind::X11;
    // Fcitx 默认连接的 name 为空，因此 "wayland:" 本身也是合法 display。
    if (display.starts_with("wayland:")) return DisplayKind::Wayland;
    return DisplayKind::Unknown;
}

BackendProbe probeBackend(const std::string &display) {
    switch (displayKind(display)) {
    case DisplayKind::X11:
#if defined(QJ_X11_BACKEND)
        return {DisplayKind::X11, true, false, "X11 承载可探测"};
#else
        return {DisplayKind::X11, false, false, "当前构建未启用 X11 承载"};
#endif
    case DisplayKind::Wayland:
        // v1 可经公开 wl_display + 生成的 C 协议探测；v2 仍暴露内部包装类型。
        // 阶段 0 未通过 GNOME/KDE surface 验收，生产构建不接入 Wayland 后端。
        return {DisplayKind::Wayland, false, false, "Wayland popup 承载尚未接入，保留默认面板"};
    case DisplayKind::Unknown:
        return {DisplayKind::Unknown, false, false, "未知输入上下文 display"};
    }
    return {};
}
}
