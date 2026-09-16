//! 显示协议选择与连接创建，不读取 DISPLAY 或 XDG_SESSION_TYPE。
#include "selector.h"
#include "probe.h"
#include "xcb.h"
namespace qingjian::panel {
std::unique_ptr<Backend> openBackend(const std::string &display, const char **reason) {
    const auto probe = probeBackend(display);
    if (reason) *reason = probe.reason;
    if (!probe.available) return {};
    switch (probe.display) {
    case DisplayKind::X11: return openXcb(display.substr(4), reason);
    case DisplayKind::Wayland:
    case DisplayKind::Unknown: return {};
    }
    return {};
}
}
