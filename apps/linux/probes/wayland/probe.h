//! 自有 Wayland 连接上的 v1 input-panel 色块；生命周期交给 Fcitx 主事件循环。
#pragma once
#include <fcitx-utils/event.h>
#include <wayland-client.h>
#include "input-method-client.h"
#include <memory>

namespace qingjian::probe {
class Probe final {
public:
    ~Probe();
    int run(const char *socket, bool surface);
private:
    void flush();
    void sync();
    void inspect();
    void show();
    void fail(const char *reason);
    static void global(void *, wl_registry *, uint32_t, const char *, uint32_t);
    static void removed(void *, wl_registry *, uint32_t);

    fcitx::EventLoop loop_;

    std::unique_ptr<fcitx::EventSourceIO> io_;

    std::unique_ptr<fcitx::EventSourceTime> timeout_;

    wl_display *display_ = nullptr;

    wl_registry *registry_ = nullptr;

    wl_compositor *compositor_ = nullptr;

    wl_shm *shm_ = nullptr;

    zwp_input_panel_v1 *panel_ = nullptr;

    wl_surface *surface_ = nullptr;

    zwp_input_panel_surface_v1 *role_ = nullptr;

    wl_buffer *buffer_ = nullptr;

    wl_callback *callback_ = nullptr;

    wl_seat *seat_ = nullptr;

    wl_pointer *pointer_ = nullptr;

    void *pixels_ = nullptr;

    uint32_t compositorId_ = 0, shmId_ = 0, panelId_ = 0;

    bool argb_ = false, v2_ = false, requested_ = false, initialized_ = false;

    bool released_ = false, frameDone_ = false, pointerFocus_ = false;

    unsigned syncCount_ = 0, clicks_ = 0;

    int result_ = 77;
};
}
