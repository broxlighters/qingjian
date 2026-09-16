//! 协议全局、buffer release、frame 与 pointer 最小探测；不申请键盘焦点。
#include "probe.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

namespace qingjian::probe {
namespace {
constexpr int width = 240, height = 64, bytes = width * height * 4;
}
Probe::~Probe() {
    timeout_.reset();
    io_.reset();
    if (callback_) wl_callback_destroy(callback_);
    if (surface_) {
        wl_surface_attach(surface_, nullptr, 0, 0);
        wl_surface_commit(surface_);
    }
    if (role_) zwp_input_panel_surface_v1_destroy(role_);
    if (surface_) wl_surface_destroy(surface_);
    if (buffer_) wl_buffer_destroy(buffer_);
    if (pointer_) wl_pointer_destroy(pointer_);
    if (seat_) wl_seat_destroy(seat_);
    if (panel_) zwp_input_panel_v1_destroy(panel_);
    if (shm_) wl_shm_destroy(shm_);
    if (compositor_) wl_compositor_destroy(compositor_);
    if (registry_) wl_registry_destroy(registry_);
    if (display_) { wl_display_flush(display_); wl_display_disconnect(display_); }
    if (pixels_) munmap(pixels_, bytes);
}
void Probe::fail(const char *reason) {
    std::fprintf(stderr, "探测失败：%s\n", reason);
    result_ = 1;
    loop_.exit();
}
int Probe::run(const char *socket, bool surface) {
    if (!socket || !*socket) return 2;
    requested_ = surface;
    display_ = wl_display_connect(socket);
    if (!display_) { std::fprintf(stderr, "无法连接指定 Wayland socket\n"); return 77; }
    registry_ = wl_display_get_registry(display_);
    const static wl_registry_listener listener{global, removed};
    wl_registry_add_listener(registry_, &listener, this);
    io_ = loop_.addIOEvent(wl_display_get_fd(display_), fcitx::IOEventFlag::In,
        [this](fcitx::EventSourceIO *, int, fcitx::IOEventFlags flags) {
            if (flags.test(fcitx::IOEventFlag::Err) || flags.test(fcitx::IOEventFlag::Hup)) {
                fail("compositor 连接断开"); return true;
            }
            if (flags.test(fcitx::IOEventFlag::In)) {
                while (wl_display_prepare_read(display_) != 0) {
                    if (wl_display_dispatch_pending(display_) < 0) { fail("协议事件失败"); return true; }
                }
                if (wl_display_read_events(display_) < 0 || wl_display_dispatch_pending(display_) < 0) {
                    fail("协议读取失败"); return true;
                }
            }
            flush();
            return true;
        });
    timeout_ = loop_.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 3000000, 0,
        [this](fcitx::EventSourceTime *, uint64_t) {
            std::printf("frame_callback=%u buffer_release=%u pointer_focus=%u clicks=%u\n",
                frameDone_, released_, pointerFocus_, clicks_);
            // frame callback 只证明 compositor 调度了帧，不证明已在实际应用光标旁显示。
            result_ = initialized_ && frameDone_ ? 0 : 77;
            loop_.exit();
            return false;
        });
    if (!io_ || !timeout_) return 1;
    sync();
    flush();
    loop_.exec();
    return result_;
}
void Probe::flush() {
    const auto result = wl_display_flush(display_);
    if (result < 0 && errno != EAGAIN) { fail("协议提交失败"); return; }
    io_->setEvents(result < 0 ? fcitx::IOEventFlags(fcitx::IOEventFlag::In) | fcitx::IOEventFlag::Out : fcitx::IOEventFlags(fcitx::IOEventFlag::In));
}
void Probe::sync() {
    callback_ = wl_display_sync(display_);
    const static wl_callback_listener listener{[](void *data, wl_callback *callback, uint32_t) {
        auto &self = *static_cast<Probe *>(data);
        wl_callback_destroy(callback);
        self.callback_ = nullptr;
        // 第一次获取 registry，第二次接收 bind 后的 shm format / seat 事件。
        if (++self.syncCount_ == 1) self.sync();
        else self.inspect();
    }};
    wl_callback_add_listener(callback_, &listener, this);
}
void Probe::global(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t) {
    auto &self = *static_cast<Probe *>(data);
    if (std::strcmp(interface, "wl_compositor") == 0) {
        self.compositorId_ = name;
        self.compositor_ = static_cast<wl_compositor *>(wl_registry_bind(registry, name, &wl_compositor_interface, 1));
    } else if (std::strcmp(interface, "wl_shm") == 0) {
        self.shmId_ = name;
        self.shm_ = static_cast<wl_shm *>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
        const static wl_shm_listener listener{[](void *data, wl_shm *, uint32_t format) {
            if (format == WL_SHM_FORMAT_ARGB8888) static_cast<Probe *>(data)->argb_ = true;
        }};
        wl_shm_add_listener(self.shm_, &listener, &self);
    } else if (std::strcmp(interface, "zwp_input_panel_v1") == 0) {
        self.panelId_ = name;
        // 仅在显式色块模式绑定 input-panel，不创建 input-method 或抢占已有输入法。
        if (self.requested_) self.panel_ = static_cast<zwp_input_panel_v1 *>(wl_registry_bind(registry, name, &zwp_input_panel_v1_interface, 1));
    } else if (std::strcmp(interface, "zwp_input_method_manager_v2") == 0) {
        self.v2_ = true;
    } else if (self.requested_ && !self.seat_ && std::strcmp(interface, "wl_seat") == 0) {
        self.seat_ = static_cast<wl_seat *>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
        const static wl_seat_listener listener{
            [](void *data, wl_seat *seat, uint32_t capabilities) {
                auto &p = *static_cast<Probe *>(data);
                if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) || p.pointer_) return;
                p.pointer_ = wl_seat_get_pointer(seat);
                static const wl_pointer_listener pointerListener = [] {
                    wl_pointer_listener events{};
                    events.enter = [](void *data, wl_pointer *, uint32_t, wl_surface *surface, wl_fixed_t, wl_fixed_t) {
                        auto &p = *static_cast<Probe *>(data);
                        if (surface == p.surface_) p.pointerFocus_ = true;
                    };
                    events.leave = [](void *, wl_pointer *, uint32_t, wl_surface *) {};
                    events.motion = [](void *, wl_pointer *, uint32_t, wl_fixed_t, wl_fixed_t) {};
                    events.button = [](void *data, wl_pointer *, uint32_t, uint32_t, uint32_t, uint32_t state) {
                        if (state == WL_POINTER_BUTTON_STATE_PRESSED) ++static_cast<Probe *>(data)->clicks_;
                    };
                    events.axis = [](void *, wl_pointer *, uint32_t, uint32_t, wl_fixed_t) {};
                    return events;
                }();
                wl_pointer_add_listener(p.pointer_, &pointerListener, &p);
            }, nullptr};
        wl_seat_add_listener(self.seat_, &listener, &self);
    }
}
void Probe::removed(void *data, wl_registry *, uint32_t name) {
    auto &self = *static_cast<Probe *>(data);
    if (name == self.compositorId_ || name == self.shmId_ || name == self.panelId_) self.fail("承载协议被移除");
}
void Probe::inspect() {
    initialized_ = true;
    std::printf("compositor=%u shm_argb8888=%u input_panel_v1=%u input_method_v2=%u\n",
        bool(compositor_), argb_, bool(panelId_), v2_);
    if (!requested_) { result_ = 0; loop_.exit(); return; }
    if (!compositor_ || !shm_ || !argb_ || !panel_) {
        std::fprintf(stderr, "缺少 v1 overlay 承载，跳过色块；不创建普通顶层窗口\n");
        loop_.exit();
        return;
    }
    show();
}
void Probe::show() {
    const int fd = memfd_create("qingjian-surface-probe", MFD_CLOEXEC);
    if (fd < 0) { fail("memfd 创建失败"); return; }
    if (ftruncate(fd, bytes) != 0) { close(fd); fail("buffer 分配失败"); return; }
    pixels_ = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pixels_ == MAP_FAILED) { pixels_ = nullptr; close(fd); fail("buffer 映射失败"); return; }
    // ARGB8888 使用本机 uint32_t 格式；不假定位图 RGBA 字节可以直接提交。
    auto *pixels = static_cast<uint32_t *>(pixels_);
    for (int i = 0; i < width * height; ++i) pixels[i] = (i % width < width / 2) ? 0xffe08020U : 0xff2080e0U;
    auto *pool = wl_shm_create_pool(shm_, fd, bytes);
    close(fd);
    buffer_ = wl_shm_pool_create_buffer(pool, 0, width, height, width * 4, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    const static wl_buffer_listener release{[](void *data, wl_buffer *) { static_cast<Probe *>(data)->released_ = true; }};
    wl_buffer_add_listener(buffer_, &release, this);
    surface_ = wl_compositor_create_surface(compositor_);
    role_ = zwp_input_panel_v1_get_input_panel_surface(panel_, surface_);
    zwp_input_panel_surface_v1_set_overlay_panel(role_);
    callback_ = wl_surface_frame(surface_);
    const static wl_callback_listener frame{[](void *data, wl_callback *callback, uint32_t) {
        auto &p = *static_cast<Probe *>(data);
        p.frameDone_ = true;
        wl_callback_destroy(callback);
        p.callback_ = nullptr;
    }};
    wl_callback_add_listener(callback_, &frame, this);
    wl_surface_attach(surface_, buffer_, 0, 0);
    wl_surface_damage(surface_, 0, 0, width, height);
    wl_surface_commit(surface_);
}
}
