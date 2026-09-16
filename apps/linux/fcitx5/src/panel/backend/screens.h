//! XCB RandR 屏幕及 EWMH 工作区缓存；只有配置通知后才重新查询。
#pragma once
#if defined(QJ_EXPERIMENTAL_X11)
#include <xcb/xcb.h>
#include <fcitx-utils/rect.h>
#include <optional>
#include <vector>
namespace qingjian::panel {
class Screens final {
public:
    bool open(xcb_connection_t *connection, xcb_window_t root);
    fcitx::Rect bounds(const fcitx::Rect &cursor);
    bool changed(const xcb_generic_event_t *event);
private:
    bool refresh();
    /// 借用窗口后端连接及 root，不独立创建 X 连接。
    xcb_connection_t *connection_ = nullptr;

    /// 当前 X screen 根窗口。
    xcb_window_t root_ = 0;

    /// RandR 扩展事件基址。
    uint8_t eventBase_ = 0;

    /// 工作区和当前桌面的 EWMH 属性。
    xcb_atom_t workAtom_ = 0, desktopAtom_ = 0;

    /// 下一次取边界前需刷新。
    bool dirty_ = true;

    /// 实际屏幕列表，保留各自物理边界。
    std::vector<fcitx::Rect> monitors_;

    /// 可选的当前桌面工作区。
    std::optional<fcitx::Rect> work_;
};
}
#endif
