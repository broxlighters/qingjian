//! 独立 D-Bus 会话模拟 portal，验证双层初始化值和单层动态通知。
#include "panel/appearance.h"
#include <fcitx-utils/event.h>
#include <cassert>
#include <ctime>
int main() {
    fcitx::EventLoop loop;
    fcitx::dbus::Bus portal(fcitx::dbus::BusType::Session);
    portal.attachEventLoop(&loop);
    assert(portal.requestName("org.freedesktop.portal.Desktop", fcitx::dbus::RequestNameFlag::None));
    auto object = portal.addObject("/org/freedesktop/portal/desktop", [](fcitx::dbus::Message &message) {
        if (message.member() != "Read") return false;
        auto reply = message.createReply();
        auto inner = fcitx::dbus::Variant(uint32_t(1));
        fcitx::dbus::Variant outer;
        outer.setRawData(std::make_shared<fcitx::dbus::Variant>(inner),
                         std::make_shared<fcitx::dbus::VariantHelper<fcitx::dbus::Variant>>());
        reply << outer;
        assert(reply.send());
        return true;
    });
    unsigned changes = 0;
    qingjian::panel::Appearance *current = nullptr;
    qingjian::panel::Appearance appearance(loop, [&] {
        ++changes;
        if (changes == 1) {
            assert(current->dark());
            auto signal = portal.createSignal("/org/freedesktop/portal/desktop",
                "org.freedesktop.portal.Settings", "SettingChanged");
            signal << std::string("org.freedesktop.appearance") << std::string("color-scheme") << fcitx::dbus::Variant(uint32_t(2));
            assert(signal.send());
        } else {
            assert(!current->dark());
            loop.exit();
        }
    });
    current = &appearance;
    auto timeout = loop.addTimeEvent(CLOCK_MONOTONIC, 0, 0,
        [&](fcitx::EventSourceTime *, uint64_t) { loop.exit(); return false; });
    timeout->setTime(fcitx::now(CLOCK_MONOTONIC) + 2000000);
    loop.exec();
    assert(changes == 2 && !appearance.dark());
}
