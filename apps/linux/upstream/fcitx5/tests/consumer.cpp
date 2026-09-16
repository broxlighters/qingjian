//! 独立公开头消费者；无私有 wrapper 头、生成协议头或 waylandim 链接。
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <memory>
#include <type_traits>
#include <fcitx/addoninstance.h>
#include <waylandim_popup_public.h>
#include <wayland-client.h>
static_assert(std::is_same_v<decltype(std::declval<fcitx::WaylandPopup &>().surface()), wl_surface *>);
static_assert(std::is_same_v<decltype(std::declval<fcitx::WaylandPopup &>().display()), wl_display *>);
void exercise(fcitx::AddonInstance *addon, fcitx::InputContext *ic) {
    if (addon->call<fcitx::IWaylandIMModule::queryPopup>(ic) != fcitx::WaylandPopupAvailability::Available) { return; }
    wl_callback *frame = nullptr;
    auto popup = addon->call<fcitx::IWaylandIMModule::createPopup>(ic, [&](auto) {
        if (frame) { wl_callback_destroy(frame); frame = nullptr; }
    });
    if (popup) {
        frame = wl_surface_frame(popup->surface());
        wl_surface_commit(popup->surface());
        popup->close();
    }
}
int main() { return 0; }
