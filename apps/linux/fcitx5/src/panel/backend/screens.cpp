//! 缺少 RandR 1.5 或有效屏幕时拒绝自绘，不用根窗口联合矩形冒充可用屏幕。
#include "screens.h"
#if defined(QJ_EXPERIMENTAL_X11)
#include "area.h"
#include <xcb/randr.h>
#include <cstdlib>
#include <cstring>
#include <memory>
namespace qingjian::panel {
namespace {
template <typename T> using Reply = std::unique_ptr<T, decltype(&std::free)>;
xcb_atom_t atom(xcb_connection_t *connection, const char *name) {
    Reply<xcb_intern_atom_reply_t> value(xcb_intern_atom_reply(connection,
        xcb_intern_atom(connection, false, std::strlen(name), name), nullptr), &std::free);
    return value ? value->atom : xcb_atom_t(XCB_NONE);
}
std::vector<uint32_t> property(xcb_connection_t *connection, xcb_window_t root, xcb_atom_t atom) {
    Reply<xcb_get_property_reply_t> reply(xcb_get_property_reply(connection,
        xcb_get_property(connection, false, root, atom, XCB_ATOM_CARDINAL, 0, 4096), nullptr), &std::free);
    if (!reply || reply->type != XCB_ATOM_CARDINAL || reply->format != 32 || reply->bytes_after) return {};
    const auto *values = static_cast<const uint32_t *>(xcb_get_property_value(reply.get()));
    return {values, values + xcb_get_property_value_length(reply.get()) / 4};
}
}
bool Screens::open(xcb_connection_t *connection, xcb_window_t root) {
    connection_ = connection; root_ = root;
    const auto *extension = xcb_get_extension_data(connection_, &xcb_randr_id);
    if (!extension || !extension->present) return false;
    eventBase_ = extension->first_event;
    Reply<xcb_randr_query_version_reply_t> version(xcb_randr_query_version_reply(connection_,
        xcb_randr_query_version(connection_, 1, 5), nullptr), &std::free);
    if (!version || version->major_version < 1 ||
        (version->major_version == 1 && version->minor_version < 5)) return false;
    workAtom_ = atom(connection_, "_NET_WORKAREA");
    desktopAtom_ = atom(connection_, "_NET_CURRENT_DESKTOP");
    if (!workAtom_ || !desktopAtom_) return false;
    const uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_change_window_attributes(connection_, root_, XCB_CW_EVENT_MASK, &mask);
    xcb_randr_select_input(connection_, root_, XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE |
        XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE | XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE |
        XCB_RANDR_NOTIFY_MASK_RESOURCE_CHANGE);
    return refresh();
}
bool Screens::refresh() {
    monitors_.clear(); work_.reset();
    Reply<xcb_randr_get_monitors_reply_t> reply(xcb_randr_get_monitors_reply(connection_,
        xcb_randr_get_monitors(connection_, root_, true), nullptr), &std::free);
    if (!reply) return false;
    for (auto i = xcb_randr_get_monitors_monitors_iterator(reply.get()); i.rem; xcb_randr_monitor_info_next(&i)) {
        const auto &m = *i.data;
        if (m.width && m.height) monitors_.emplace_back(m.x, m.y, int(m.x) + m.width, int(m.y) + m.height);
    }
    auto desktop = property(connection_, root_, desktopAtom_);
    auto work = property(connection_, root_, workAtom_);
    if (!work.empty()) {
        if (work.size() % 4 != 0) return false;
        size_t offset = 0;
        if (desktop.size() == 1 && desktop[0] < work.size() / 4) offset = size_t(desktop[0]) * 4;
        else {
            // GNOME XWayland 可能不报告当前桌面；仅所有工作区相同时才采用共同值。
            for (size_t i = 4; i < work.size(); ++i) if (work[i] != work[i % 4]) return false;
        }
        const auto x = int32_t(work[offset]), y = int32_t(work[offset + 1]);
        const auto width = work[offset + 2], height = work[offset + 3];
        if (width && height && width <= 65535 && height <= 65535 &&
            int64_t(x) + width <= INT32_MAX && int64_t(y) + height <= INT32_MAX)
            work_ = fcitx::Rect(x, y, x + int(width), y + int(height));
        else return false;
    }
    dirty_ = false;
    return !monitors_.empty();
}
fcitx::Rect Screens::bounds(const fcitx::Rect &cursor) {
    if (dirty_ && !refresh()) return fcitx::Rect();
    return availableArea(monitors_, work_, cursor);
}
bool Screens::changed(const xcb_generic_event_t *event) {
    const auto type = event->response_type & 0x7f;
    bool changed = type == eventBase_ + XCB_RANDR_SCREEN_CHANGE_NOTIFY || type == eventBase_ + XCB_RANDR_NOTIFY;
    if (type == XCB_PROPERTY_NOTIFY) {
        const auto *property = reinterpret_cast<const xcb_property_notify_event_t *>(event);
        changed |= property->window == root_ && (property->atom == workAtom_ || property->atom == desktopAtom_);
    }
    dirty_ |= changed;
    return changed;
}
}
#endif
