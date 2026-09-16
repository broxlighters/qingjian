//! 非激活 ARGB 原型；只允许显式实验构建，不代表桌面验收已通过。
#include "xcb.h"
#include "placement.h"
#include "screens.h"
#if defined(QJ_EXPERIMENTAL_X11)
#include <xcb/xcb.h>
#include <xcb/render.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <tuple>

namespace qingjian::panel {
namespace {
template <typename T> using Reply = std::unique_ptr<T, decltype(&std::free)>;
class XcbBackend final : public Backend {
public:
    ~XcbBackend() override {
        if (connection_) xcb_disconnect(connection_);
    }
    bool open(const std::string &display) {
        int index = 0;
        connection_ = xcb_connect(display.c_str(), &index);
        if (!connection_ || xcb_connection_has_error(connection_)) return false;
        const auto *setup = xcb_get_setup(connection_);
        if (setup->image_byte_order != XCB_IMAGE_ORDER_LSB_FIRST) return false;
        auto screens = xcb_setup_roots_iterator(setup);
        for (int i = 0; i < index && screens.rem; ++i) xcb_screen_next(&screens);
        if (!screens.rem) return false;
        screen_ = screens.data;
        if (!screens_.open(connection_, screen_->root)) return false;
        // 必须有合成器和明确的 ARGB8888 visual，不能把 RGBA 当作 root visual。
        auto selection = std::string("_NET_WM_CM_S") + std::to_string(index);
        Reply<xcb_intern_atom_reply_t> atom(xcb_intern_atom_reply(connection_,
            xcb_intern_atom(connection_, false, selection.size(), selection.c_str()), nullptr), &std::free);
        if (!atom) return false;
        Reply<xcb_get_selection_owner_reply_t> owner(xcb_get_selection_owner_reply(connection_,
            xcb_get_selection_owner(connection_, atom->atom), nullptr), &std::free);
        if (!owner || owner->owner == XCB_NONE) return false;
        Reply<xcb_render_query_pict_formats_reply_t> formats(xcb_render_query_pict_formats_reply(
            connection_, xcb_render_query_pict_formats(connection_), nullptr), &std::free);
        if (!formats) return false;
        xcb_render_pictformat_t argb = 0;
        for (auto i = xcb_render_query_pict_formats_formats_iterator(formats.get()); i.rem;
             xcb_render_pictforminfo_next(&i)) {
            const auto &f = *i.data;
            if (f.type == XCB_RENDER_PICT_TYPE_DIRECT && f.depth == 32 &&
                f.direct.red_shift == 16 && f.direct.green_shift == 8 &&
                f.direct.blue_shift == 0 && f.direct.alpha_shift == 24 &&
                f.direct.red_mask == 255 && f.direct.green_mask == 255 &&
                f.direct.blue_mask == 255 && f.direct.alpha_mask == 255) argb = f.id;
        }
        xcb_visualid_t visual = 0;
        auto renderScreen = xcb_render_query_pict_formats_screens_iterator(formats.get());
        for (int i = 0; i < index && renderScreen.rem; ++i) xcb_render_pictscreen_next(&renderScreen);
        if (!renderScreen.rem || !argb) return false;
        for (auto depth = xcb_render_pictscreen_depths_iterator(renderScreen.data); depth.rem;
             xcb_render_pictdepth_next(&depth)) {
            for (auto v = xcb_render_pictdepth_visuals_iterator(depth.data); v.rem;
                 xcb_render_pictvisual_next(&v)) if (v.data->format == argb) visual = v.data->visual;
        }
        if (!visual) return false;
        const auto colormap = xcb_generate_id(connection_);
        if (!check(xcb_create_colormap_checked(connection_, XCB_COLORMAP_ALLOC_NONE,
                                              colormap, screen_->root, visual))) return false;
        window_ = xcb_generate_id(connection_);
        const uint32_t values[] = {0, 0, 1, XCB_EVENT_MASK_BUTTON_PRESS, colormap};
        if (!check(xcb_create_window_checked(connection_, 32, window_, screen_->root,
            0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, visual,
            XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT |
            XCB_CW_EVENT_MASK | XCB_CW_COLORMAP, values))) return false;
        gc_ = xcb_generate_id(connection_);
        return check(xcb_create_gc_checked(connection_, gc_, window_, 0, nullptr));
    }
    fcitx::Rect bounds(const fcitx::Rect &cursor) override {
        return screens_.bounds(cursor);
    }
    bool present(const uint8_t *pixels, uint32_t width, uint32_t height,
                 uint32_t stride, const fcitx::Rect &cursor) override {
        const auto area = bounds(cursor);
        if (!pixels || !width || !height || width > 8192 || height > 8192 ||
            area.width() <= 0 || area.height() <= 0 || stride < width * 4 ||
            width > unsigned(area.width()) || height > unsigned(area.height())) return false;
        const auto [x, y] = place(area, cursor, width, height);
        auto pixmap = xcb_generate_id(connection_);
        if (!check(xcb_create_pixmap_checked(connection_, 32, pixmap, window_, width, height))) return false;
        std::vector<uint8_t> bgra(static_cast<size_t>(width) * height * 4);
        for (uint32_t row = 0; row < height; ++row) {
            for (uint32_t col = 0; col < width; ++col) {
                const auto *source = pixels + static_cast<size_t>(row) * stride + col * 4;
                auto *dest = bgra.data() + (static_cast<size_t>(row) * width + col) * 4;
                dest[0] = source[2]; dest[1] = source[1]; dest[2] = source[0]; dest[3] = source[3];
            }
        }
        const uint32_t maxBytes = xcb_get_maximum_request_length(connection_) * 4 - 64;
        const auto rows = std::max(1U, maxBytes / (width * 4));
        for (uint32_t row = 0; row < height; row += rows) {
            auto count = std::min(rows, height - row);
            if (!check(xcb_put_image_checked(connection_, XCB_IMAGE_FORMAT_Z_PIXMAP,
                    pixmap, gc_, width, count, 0, row, 0, 32,
                    width * count * 4, bgra.data() + static_cast<size_t>(row) * width * 4))) {
                xcb_free_pixmap(connection_, pixmap);
                return false;
            }
        }
        uint32_t config[] = {static_cast<uint32_t>(x), static_cast<uint32_t>(y), width, height};
        xcb_configure_window(connection_, window_, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                             XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, config);
        xcb_change_window_attributes(connection_, window_, XCB_CW_BACK_PIXMAP, &pixmap);
        xcb_clear_area(connection_, false, window_, 0, 0, 0, 0);
        auto mapped = xcb_map_window_checked(connection_, window_);
        sequence_ = mapped.sequence;
        bool ok = check(mapped);
        xcb_free_pixmap(connection_, pixmap);
        xcb_flush(connection_);
        return ok && !xcb_connection_has_error(connection_);
    }
    void hide() override {
        if (connection_ && window_) {
            xcb_unmap_window(connection_, window_);
            xcb_flush(connection_);
        }
        sequence_ = 0;
        drain();
    }
    void drain() override {
        while (auto *event = xcb_poll_for_event(connection_)) {
            screens_.changed(event);
            std::free(event);
        }
    }
    bool poll(const std::function<bool(int, int, unsigned)> &click) override {
        std::vector<std::tuple<int, int, unsigned>> buttons;
        while (auto *raw = xcb_poll_for_event(connection_)) {
            Reply<xcb_generic_event_t> event(raw, &std::free);
            if ((event->response_type & 0x7f) == 0) return false;
            // 先撤下旧几何的窗口，下一次有效帧按最新屏幕/工作区重新排版。
            if (screens_.changed(event.get())) return false;
            // XCB full_sequence 绑定提交顺序，旧窗口排队的事件不能点到新页。
            if (sequence_ && static_cast<int32_t>(event->full_sequence - sequence_) >= 0 &&
                (event->response_type & 0x7f) == XCB_BUTTON_PRESS) {
                auto *button = reinterpret_cast<xcb_button_press_event_t *>(raw);
                buttons.emplace_back(button->event_x, button->event_y, button->detail);
            }
        }
        if (xcb_connection_has_error(connection_)) return false;
        // 先读尽 XCB 内部队列；fd 未必还可读，不能等下一次唤醒。
        // 回调可能换帧甚至销毁后端，之后只访问本地事件，不再访问 this。
        for (const auto &[x, y, button] : buttons)
            if (!click(x, y, button)) break;
        return true;
    }
    int fd() const override { return xcb_get_file_descriptor(connection_); }
private:
    bool check(xcb_void_cookie_t cookie) {
        Reply<xcb_generic_error_t> error(xcb_request_check(connection_, cookie), &std::free);
        return !error && !xcb_connection_has_error(connection_);
    }
    /// 单独连接，销毁时窗口、GC、pixmap 全部由 X server 回收。
    xcb_connection_t *connection_ = nullptr;

    /// 此连接选中的根屏幕。
    xcb_screen_t *screen_ = nullptr;

    /// 当前物理屏幕与桌面保留区缓存。
    Screens screens_;

    /// 非激活 override-redirect 窗口。
    xcb_window_t window_ = 0;

    /// 32 位上传 GC。
    xcb_gcontext_t gc_ = 0;

    /// 最新贴图提交对应的 X 请求序号。
    uint32_t sequence_ = 0;
};
}
std::unique_ptr<Backend> openXcb(const std::string &display) {
    auto backend = std::make_unique<XcbBackend>();
    if (!backend->open(display)) return {};
    return backend;
}
}
#else
namespace qingjian::panel {
std::unique_ptr<Backend> openXcb(const std::string &) { return {}; }
}
#endif
