//! 可选真实 X server 冒烟：固定色块验证几何、像素通道、非激活与销毁；不算应用验收。
#include "panel/backend/xcb.h"
#include "panel/backend/placement.h"
#include <xcb/xcb.h>
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <vector>
#include <thread>
#include <chrono>
template <typename T> using Reply = std::unique_ptr<T, decltype(&std::free)>;
std::vector<xcb_window_t> children(xcb_connection_t *connection, xcb_window_t root) {
    Reply<xcb_query_tree_reply_t> reply(xcb_query_tree_reply(connection, xcb_query_tree(connection, root), nullptr), &std::free);
    assert(reply);
    const auto *first = xcb_query_tree_children(reply.get());
    return {first, first + xcb_query_tree_children_length(reply.get())};
}
int main() {
    const char *display = std::getenv("QINGJIAN_XCB_SMOKE_DISPLAY");
    if (!display || !*display) return 77;
    int index = 0;
    auto *connection = xcb_connect(display, &index);
    assert(connection && !xcb_connection_has_error(connection));
    auto screens = xcb_setup_roots_iterator(xcb_get_setup(connection));
    for (int i = 0; i < index; ++i) xcb_screen_next(&screens);
    assert(screens.rem);
    const auto root = screens.data->root;
    const auto before = children(connection, root);
    Reply<xcb_get_input_focus_reply_t> focus(xcb_get_input_focus_reply(connection, xcb_get_input_focus(connection), nullptr), &std::free);
    assert(focus);
    auto backend = qingjian::panel::openXcb(display);
    if (!backend) {
        fprintf(stderr, "实验 XCB 不满足合成器、ARGB 或 RandR 条件，保留默认面板\n");
        xcb_disconnect(connection);
        return 77;
    }
    auto after = children(connection, root);
    after.erase(std::remove_if(after.begin(), after.end(), [&](auto id) {
        return std::find(before.begin(), before.end(), id) != before.end();
    }), after.end());
    assert(after.size() == 1);
    const auto window = after.front();
    Reply<xcb_get_window_attributes_reply_t> attributes(xcb_get_window_attributes_reply(connection,
        xcb_get_window_attributes(connection, window), nullptr), &std::free);
    assert(attributes && attributes->map_state == XCB_MAP_STATE_UNMAPPED && attributes->override_redirect);
    const auto area = backend->bounds(fcitx::Rect(100, 100, 101, 120));
    assert(area.width() >= 100 && area.height() >= 40);
    std::vector<uint8_t> pixels(100 * 40 * 4);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = 220; pixels[i + 1] = 70; pixels[i + 2] = 30; pixels[i + 3] = 255;
    }
    const fcitx::Rect cursor(area.right() - 1, area.bottom() - 20, area.right(), area.bottom());
    assert(backend->present(pixels.data(), 100, 40, 400, cursor));
    Reply<xcb_get_geometry_reply_t> geometry(xcb_get_geometry_reply(connection, xcb_get_geometry(connection, window), nullptr), &std::free);
    assert(geometry && geometry->x == area.right() - 100 && geometry->y == cursor.top() - 40);
    Reply<xcb_get_input_focus_reply_t> afterFocus(xcb_get_input_focus_reply(connection, xcb_get_input_focus(connection), nullptr), &std::free);
    assert(afterFocus && afterFocus->focus == focus->focus);
    Reply<xcb_get_image_reply_t> image(xcb_get_image_reply(connection,
        xcb_get_image(connection, XCB_IMAGE_FORMAT_Z_PIXMAP, window, 0, 0, 1, 1, 0xffffffff), nullptr), &std::free);
    assert(image && xcb_get_image_data_length(image.get()) >= 4);
    const auto *bgra = xcb_get_image_data(image.get());
    assert(bgra[0] == 30 && bgra[1] == 70 && bgra[2] == 220);
    backend->hide();
    for (unsigned i = 0; i < 100; ++i) {
        attributes.reset(xcb_get_window_attributes_reply(connection, xcb_get_window_attributes(connection, window), nullptr));
        if (attributes && attributes->map_state == XCB_MAP_STATE_UNMAPPED) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(attributes && attributes->map_state == XCB_MAP_STATE_UNMAPPED);
    for (const auto &corner : {fcitx::Rect(area.left(), area.top(), area.left() + 1, area.top() + 20),
                               fcitx::Rect(area.right() - 1, area.top(), area.right(), area.top() + 20),
                               fcitx::Rect(area.left(), area.bottom() - 20, area.left() + 1, area.bottom())}) {
        assert(backend->present(pixels.data(), 100, 40, 400, corner));
        geometry.reset(xcb_get_geometry_reply(connection, xcb_get_geometry(connection, window), nullptr));
        const auto expected = qingjian::panel::place(area, corner, 100, 40);
        assert(geometry && geometry->x == expected.first && geometry->y == expected.second);
    }
    auto queueClicks = [&] {
        xcb_button_press_event_t event{};
        event.response_type = XCB_BUTTON_PRESS;
        event.event = window;
        event.root = root;
        event.detail = 1;
        event.event_x = 10;
        event.event_y = 20;
        for (unsigned i = 0; i < 2; ++i)
            xcb_send_event(connection, false, window, XCB_EVENT_MASK_BUTTON_PRESS,
                           reinterpret_cast<const char *>(&event));
        // 同步请求确保服务器已将两次事件都交付给被测连接。
        geometry.reset(xcb_get_geometry_reply(connection, xcb_get_geometry(connection, window), nullptr));
        assert(geometry);
    };
    queueClicks();
    unsigned clicks = 0;
    assert(backend->poll([&](int x, int y, unsigned button) {
        assert(x == 10 && y == 20 && button == 1);
        ++clicks;
        return true;
    }));
    assert(clicks == 2); // 忽略一次点击后仍能处理 XCB 已缓存的后续事件。
    queueClicks();
    clicks = 0;
    assert(backend->poll([&](int, int, unsigned) { ++clicks; return false; }));
    assert(clicks == 1);
    assert(backend->poll([](int, int, unsigned) { assert(false); return false; }));
    backend.reset();
    for (unsigned i = 0; i < 100; ++i) {
        after = children(connection, root);
        if (std::find(after.begin(), after.end(), window) == after.end()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(std::find(after.begin(), after.end(), window) == after.end());
    xcb_disconnect(connection);
    fprintf(stderr, "真实 X server：窗口初始隐藏、四边定位、RGBA/BGRA、非激活和销毁已通过\n");
}
