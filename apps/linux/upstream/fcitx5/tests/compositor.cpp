//! 使用 libwayland-server 校验真实 addon 的同连接 role 与销毁请求。
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "compositor.h"
#include <cassert>
#include <sys/socket.h>
#include <unistd.h>
#include <wayland-server.h>
#include <fcitx-utils/event.h>
#include "im-server.h"
#include "vk-server.h"

namespace {
void destroy(wl_client *, wl_resource *resource) { wl_resource_destroy(resource); }
Compositor &owner(wl_resource *r) {
    return *static_cast<Compositor *>(wl_resource_get_user_data(r));
}
const struct wl_surface_interface surfaceImpl = {
    .destroy = destroy,
    .attach = [](wl_client *, wl_resource *r, wl_resource *buffer, int32_t, int32_t) {
        if (!buffer) { ++owner(r).hidden; }
    },
    .damage = [](wl_client *, wl_resource *, int32_t, int32_t, int32_t, int32_t) {},
    .frame = [](wl_client *c, wl_resource *r, uint32_t id) {
        auto *cb = wl_resource_create(c, &wl_callback_interface, 1, id);
        ++owner(r).frames;
        // 保持未完成，关闭路径必须由消费者销毁 client callback。
        wl_resource_set_implementation(cb, nullptr, nullptr, nullptr);
    },
    .set_opaque_region = [](wl_client *, wl_resource *, wl_resource *) {},
    .set_input_region = [](wl_client *, wl_resource *, wl_resource *) {},
    .commit = [](wl_client *, wl_resource *r) { ++owner(r).commits; },
    .set_buffer_transform = [](wl_client *, wl_resource *, int32_t) {},
    .set_buffer_scale = [](wl_client *, wl_resource *, int32_t) {},
    .damage_buffer = [](wl_client *, wl_resource *, int32_t, int32_t, int32_t, int32_t) {},
    .offset = [](wl_client *, wl_resource *, int32_t, int32_t) {},
};
const struct wl_region_interface regionImpl = {
    .destroy = destroy,
    .add = [](wl_client *, wl_resource *, int32_t, int32_t, int32_t, int32_t) {},
    .subtract = [](wl_client *, wl_resource *, int32_t, int32_t, int32_t, int32_t) {},
};
const struct wl_compositor_interface compositorImpl = {
    .create_surface = [](wl_client *c, wl_resource *r, uint32_t id) {
        auto *s = wl_resource_create(c, &wl_surface_interface, wl_resource_get_version(r), id);
        ++owner(r).surfaces;
        wl_resource_set_implementation(s, &surfaceImpl, &owner(r), [](wl_resource *s) { --owner(s).surfaces; });
    },
    .create_region = [](wl_client *c, wl_resource *, uint32_t id) {
        wl_resource_set_implementation(wl_resource_create(c, &wl_region_interface, 1, id), &regionImpl, nullptr, nullptr);
    },
};
const struct wl_seat_interface seatImpl = {
    .get_pointer = [](wl_client *, wl_resource *, uint32_t) { assert(false); },
    .get_keyboard = [](wl_client *, wl_resource *, uint32_t) { assert(false); },
    .get_touch = [](wl_client *, wl_resource *, uint32_t) { assert(false); },
    .release = destroy,
};
const struct zwp_input_popup_surface_v2_interface popupImpl = { .destroy = destroy };
const struct zwp_input_method_keyboard_grab_v2_interface grabImpl = { .release = destroy };
const struct zwp_input_method_v2_interface imImpl = {
    .commit_string = [](wl_client *, wl_resource *, const char *) {},
    .set_preedit_string = [](wl_client *, wl_resource *, const char *, int32_t, int32_t) {},
    .delete_surrounding_text = [](wl_client *, wl_resource *, uint32_t, uint32_t) {},
    .commit = [](wl_client *, wl_resource *, uint32_t) {},
    .get_input_popup_surface = [](wl_client *c, wl_resource *r, uint32_t id, wl_resource *surface) {
        // server 解析对象参数本身即校验了同连接；再断言其实现与归属。
        assert(wl_resource_get_client(surface) == c);
        assert(wl_resource_instance_of(surface, &wl_surface_interface, &surfaceImpl));
        auto *p = wl_resource_create(c, &zwp_input_popup_surface_v2_interface, 1, id);
        ++owner(r).roles;
        wl_resource_set_implementation(p, &popupImpl, &owner(r), [](wl_resource *p) { --owner(p).roles; });
        zwp_input_popup_surface_v2_send_text_input_rectangle(p, 3, 4, 5, 6);
    },
    .grab_keyboard = [](wl_client *c, wl_resource *, uint32_t id) {
        auto *g = wl_resource_create(c, &zwp_input_method_keyboard_grab_v2_interface, 1, id);
        wl_resource_set_implementation(g, &grabImpl, nullptr, nullptr);
    },
    .destroy = destroy,
};
const struct zwp_input_method_manager_v2_interface imManagerImpl = {
    .get_input_method = [](wl_client *c, wl_resource *r, wl_resource *, uint32_t id) {
        auto &s = owner(r);
        s.im = wl_resource_create(c, &zwp_input_method_v2_interface, 1, id);
        wl_resource_set_implementation(s.im, &imImpl, &s, [](wl_resource *r) { owner(r).im = nullptr; });
    },
    .destroy = destroy,
};
const struct zwp_virtual_keyboard_v1_interface vkImpl = {
    .keymap = [](wl_client *, wl_resource *, uint32_t, int32_t fd, uint32_t) { close(fd); },
    .key = [](wl_client *, wl_resource *, uint32_t, uint32_t, uint32_t) {},
    .modifiers = [](wl_client *, wl_resource *, uint32_t, uint32_t, uint32_t, uint32_t) {},
    .destroy = destroy,
};
const struct zwp_virtual_keyboard_manager_v1_interface vkManagerImpl = {
    .create_virtual_keyboard = [](wl_client *c, wl_resource *, wl_resource *, uint32_t id) {
        wl_resource_set_implementation(wl_resource_create(c, &zwp_virtual_keyboard_v1_interface, 1, id), &vkImpl, nullptr, nullptr);
    },
};
}
Compositor::Compositor(fcitx::EventLoop &loop, bool withCompositor) {
    display = wl_display_create();
    assert(display);
    assert(wl_display_init_shm(display) == 0);
    if (withCompositor) {
        compositor = wl_global_create(display, &wl_compositor_interface, 4, this,
            [](wl_client *c, void *data, uint32_t version, uint32_t id) {
                wl_resource_set_implementation(wl_resource_create(c, &wl_compositor_interface, version, id), &compositorImpl, data, nullptr);
            });
    }
    wl_global_create(display, &wl_seat_interface, 5, this,
        [](wl_client *c, void *data, uint32_t version, uint32_t id) {
            auto *r = wl_resource_create(c, &wl_seat_interface, version, id);
            wl_resource_set_implementation(r, &seatImpl, data, nullptr);
            wl_seat_send_capabilities(r, 0);
            wl_seat_send_name(r, "popup-test");
        });
    wl_global_create(display, &zwp_input_method_manager_v2_interface, 1, this,
        [](wl_client *c, void *data, uint32_t, uint32_t id) {
            wl_resource_set_implementation(wl_resource_create(c, &zwp_input_method_manager_v2_interface, 1, id), &imManagerImpl, data, nullptr);
        });
    wl_global_create(display, &zwp_virtual_keyboard_manager_v1_interface, 1, this,
        [](wl_client *c, void *data, uint32_t, uint32_t id) {
            wl_resource_set_implementation(wl_resource_create(c, &zwp_virtual_keyboard_manager_v1_interface, 1, id), &vkManagerImpl, data, nullptr);
        });
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) == 0);
    client = wl_client_create(display, sockets[0]);
    assert(client);
    fd_ = sockets[1];
    auto *serverLoop = wl_display_get_event_loop(display);
    io_ = loop.addIOEvent(wl_event_loop_get_fd(serverLoop), fcitx::IOEventFlag::In,
        [this, serverLoop](auto *, int, auto) {
            ++dispatches;
            assert(wl_event_loop_dispatch(serverLoop, 0) == 0);
            flush();
            return true;
        });
}
Compositor::~Compositor() {
    io_.reset();
    if (fd_ >= 0) { close(fd_); }
    wl_display_destroy_clients(display);
    wl_display_destroy(display);
}
int Compositor::takeClientFd() { int fd = fd_; fd_ = -1; return fd; }
void Compositor::flush() { wl_display_flush_clients(display); }
void Compositor::activate() { assert(im); zwp_input_method_v2_send_activate(im); zwp_input_method_v2_send_done(im); flush(); }
void Compositor::deactivate() { assert(im); zwp_input_method_v2_send_deactivate(im); zwp_input_method_v2_send_done(im); flush(); }
void Compositor::unavailable() { assert(im); zwp_input_method_v2_send_unavailable(im); flush(); }
void Compositor::disconnect() { if (client) { wl_client_destroy(client); client = nullptr; } if (fd_ >= 0) { close(fd_); fd_ = -1; } }
void Compositor::removeCompositor() { wl_global_remove(compositor); flush(); }
