//! 内存 socket 上的协议测试 compositor；不模拟桌面定位与可见性。
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <memory>
#include <fcitx-utils/eventloopinterface.h>
struct wl_display;
struct wl_client;
struct wl_resource;
struct wl_global;
namespace fcitx { class EventLoop; }
class Compositor {
public:
    explicit Compositor(fcitx::EventLoop &loop, bool withCompositor = true);
    ~Compositor();
    int takeClientFd();
    void activate();
    void deactivate();
    void unavailable();
    void disconnect();
    void removeCompositor();
    void flush();

    wl_display *display = nullptr;
    wl_client *client = nullptr;
    wl_resource *im = nullptr;
    wl_global *compositor = nullptr;
    int surfaces = 0;
    int roles = 0;
    int hidden = 0;
    int frames = 0;
    int commits = 0;
    int dispatches = 0;

private:
    int fd_ = -1;
    std::unique_ptr<fcitx::EventSourceIO> io_;
};
