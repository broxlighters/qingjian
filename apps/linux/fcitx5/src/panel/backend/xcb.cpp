//! X11 非激活 ARGB 候选窗；承载失败时由 Controller 回退默认面板。
#include "xcb.h"
#include "placement.h"
#include "screens.h"
#include "upload.h"
#if defined(QJ_X11_BACKEND)
#include <xcb/xcb.h>
#include <xcb/xcbext.h>
#include <xcb/render.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <tuple>
#include <chrono>

namespace qingjian::panel {
namespace {
template <typename T> using Reply = std::unique_ptr<T, decltype(&std::free)>;
class XcbBackend final : public Backend {
public:
    ~XcbBackend() override {
        if (connection_) xcb_disconnect(connection_);
    }
    bool open(const std::string &display) {
        reason_ = "X11 display 为空";
        if (display.empty()) return false; // 禁止 xcb_connect 隐式使用 DISPLAY。
        reason_ = "X11 连接不可用";
        int index = 0;
        connection_ = xcb_connect(display.c_str(), &index);
        if (!connection_ || xcb_connection_has_error(connection_)) return false;
        const auto *setup = xcb_get_setup(connection_);
        reason_ = "X11 服务端像素字节序不受支持";
        if (setup->image_byte_order != XCB_IMAGE_ORDER_LSB_FIRST) return false;
        auto screens = xcb_setup_roots_iterator(setup);
        reason_ = "X11 根屏幕不可用";
        for (int i = 0; i < index && screens.rem; ++i) xcb_screen_next(&screens);
        if (!screens.rem) return false;
        screen_ = screens.data;
        reason_ = "X11 RandR 1.5 或屏幕可用区域不可用";
        if (!screens_.open(connection_, screen_->root)) return false;
        // 必须有合成器和明确的 ARGB8888 visual，不能把 RGBA 当作 root visual。
        auto selection = std::string("_NET_WM_CM_S") + std::to_string(index);
        reason_ = "X11 合成器不可用";
        Reply<xcb_intern_atom_reply_t> atom(xcb_intern_atom_reply(connection_,
            xcb_intern_atom(connection_, false, selection.size(), selection.c_str()), nullptr), &std::free);
        if (!atom) return false;
        Reply<xcb_get_selection_owner_reply_t> owner(xcb_get_selection_owner_reply(connection_,
            xcb_get_selection_owner(connection_, atom->atom), nullptr), &std::free);
        if (!owner || owner->owner == XCB_NONE) return false;
        compositorAtom_ = atom->atom;
        compositorOwner_ = owner->owner;
        reason_ = "X11 ARGB8888 visual 不可用";
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
        reason_ = "X11 非激活窗口创建失败";
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
    const char *failureReason() const { return reason_; }
    fcitx::Rect bounds(const fcitx::Rect &cursor) override {
        return screens_.bounds(cursor);
    }
    bool present(const uint8_t *pixels, uint32_t width, uint32_t height,
                 uint32_t stride, const fcitx::Rect &cursor) override {
        timing_ = {};
        const auto probe = std::chrono::steady_clock::now();
        if (!healthy()) return false;
        const auto area = bounds(cursor);
        timing_.probeNs = nanoseconds(std::chrono::steady_clock::now() - probe);
        if (!pixels || !width || !height || width > 1600 || height > 900 ||
            area.width() <= 0 || area.height() <= 0 || stride < width * 4 ||
            width > unsigned(area.width()) || height > unsigned(area.height())) return false;
        const auto [x, y] = place(area, cursor, width, height);
        const bool resized = !pixmap_ || width != width_ || height != height_;
        if (resized) {
            discardPixmap();
            const auto pixmap = xcb_generate_id(connection_);
            if (!check(xcb_create_pixmap_checked(connection_, 32, pixmap, window_, width, height))) return false;
            pixmap_ = pixmap;
            width_ = width; height_ = height;
        }
        const auto convert = std::chrono::steady_clock::now();
        if (!pixels_.prepare(pixels, width, height, stride, resized)) { discardPixmap(); return false; }
        const auto upload = std::chrono::steady_clock::now();
        timing_.convertNs = nanoseconds(upload - convert);
        const uint64_t maxBytes = uint64_t(xcb_get_maximum_request_length(connection_)) * 4;
        if (maxBytes <= 64 + width * 4) { discardPixmap(); return false; }
        const auto rows = static_cast<uint32_t>(std::min<uint64_t>(height, (maxBytes - 64) / (width * 4)));
        const auto end = pixels_.firstRow() + pixels_.rowCount();
        for (uint32_t row = pixels_.firstRow(); row < end; row += rows) {
            auto count = std::min(rows, end - row);
            if (!check(xcb_put_image_checked(connection_, XCB_IMAGE_FORMAT_Z_PIXMAP,
                    pixmap_, gc_, width, count, 0, row, 0, 32,
                    width * count * 4, pixels_.data() + static_cast<size_t>(row) * width * 4))) {
                discardPixmap();
                return false;
            }
            timing_.uploadBytes += uint64_t(width) * count * 4;
        }
        const auto commit = std::chrono::steady_clock::now();
        timing_.uploadNs = nanoseconds(commit - upload);
        uint32_t config[] = {static_cast<uint32_t>(x), static_cast<uint32_t>(y), width, height};
        if (!check(xcb_configure_window_checked(connection_, window_, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                             XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, config)) ||
            (resized && !check(xcb_change_window_attributes_checked(connection_, window_, XCB_CW_BACK_PIXMAP, &pixmap_))) ||
            !check(xcb_clear_area_checked(connection_, false, window_, 0, 0, 0, 0))) {
            discardPixmap();
            return false;
        }
        auto mapped = xcb_map_window_checked(connection_, window_);
        sequence_ = mapped.sequence;
        bool ok = check(mapped);
        xcb_flush(connection_);
        timing_.commitNs = nanoseconds(std::chrono::steady_clock::now() - commit);
        if (!ok || xcb_connection_has_error(connection_)) { discardPixmap(); return false; }
        return true;
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
        if (!connection_) return;
        while (auto *event = xcb_poll_for_event(connection_)) {
            screens_.changed(event);
            std::free(event);
        }
    }
    bool poll(const std::function<bool(int, int, unsigned)> &click) override {
        if (!healthy()) return false;
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
    bool healthy() override {
        if (!ownerValid_ || !connection_ || xcb_connection_has_error(connection_)) return false;
        const auto now = std::chrono::steady_clock::now();
        if (ownerPending_) {
            void *raw = nullptr;
            xcb_generic_error_t *rawError = nullptr;
            const bool ready = xcb_poll_for_reply(connection_, ownerPending_, &raw, &rawError);
            Reply<xcb_get_selection_owner_reply_t> owner(static_cast<xcb_get_selection_owner_reply_t *>(raw), &std::free);
            Reply<xcb_generic_error_t> error(rawError, &std::free);
            if (ready) {
                ownerPending_ = 0;
                if (error || !owner || owner->owner == XCB_NONE || owner->owner != compositorOwner_) {
                    ownerValid_ = false;
                    return false;
                }
            } else if (now - ownerQuery_ > std::chrono::seconds(1)) {
                ownerValid_ = false;
                return false; // 服务端不响应也要撤下，不阻塞输入等待 reply。
            }
        }
        if (!ownerPending_ && now - ownerQuery_ >= std::chrono::milliseconds(250)) {
            ownerPending_ = xcb_get_selection_owner(connection_, compositorAtom_).sequence;
            ownerQuery_ = now;
            xcb_flush(connection_);
        }
        return !xcb_connection_has_error(connection_);
    }
    SubmissionTiming timing() const override { return timing_; }
private:
    static uint64_t nanoseconds(std::chrono::steady_clock::duration duration) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    }
    void discardPixmap() {
        if (pixmap_) xcb_free_pixmap(connection_, pixmap_);
        pixmap_ = 0;
    }
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

    /// 合成器 selection 所有者改变后重新探测 ARGB 能力。
    xcb_atom_t compositorAtom_ = 0;

    xcb_window_t compositorOwner_ = 0;

    /// selection query 通过正常 fd/定时器轮询 reply，不在每帧等待往返。
    unsigned ownerPending_ = 0;

    bool ownerValid_ = true;

    std::chrono::steady_clock::time_point ownerQuery_ = std::chrono::steady_clock::now();

    /// 同尺寸复用服务端背景，只上传变化行；失效后下一次必须完整上传。
    xcb_pixmap_t pixmap_ = 0;

    uint32_t width_ = 0, height_ = 0;

    PixelUpload pixels_;

    SubmissionTiming timing_;

    /// 静态诊断文案不包含 display 地址或用户输入。
    const char *reason_ = "XCB 承载不可用";
};
}
std::unique_ptr<Backend> openXcb(const std::string &display, const char **reason) {
    auto backend = std::make_unique<XcbBackend>();
    if (!backend->open(display)) {
        if (reason) *reason = backend->failureReason();
        return {};
    }
    if (reason) *reason = nullptr;
    return backend;
}
}
#else
namespace qingjian::panel {
std::unique_ptr<Backend> openXcb(const std::string &, const char **reason) {
    if (reason) *reason = "当前构建未启用 X11 承载";
    return {};
}
}
#endif
