//! 真正私有 D-Bus 的双层初始化文字倍率和动态设置通知。
#include "panel/appearance.h"
#include <fcitx-utils/event.h>
#include <cassert>
#include <limits>
int main() {
    fcitx::EventLoop loop;
    fcitx::dbus::Bus portal(fcitx::dbus::BusType::Session);
    portal.attachEventLoop(&loop);
    assert(portal.requestName("org.freedesktop.portal.Desktop", fcitx::dbus::RequestNameFlag::None));
    auto object = portal.addObject("/org/freedesktop/portal/desktop", [](fcitx::dbus::Message &message) {
        if (message.member() != "Read") return false;
        std::string space, key;
        message >> space >> key;
        auto reply = message.createReply();
        auto inner = key == "text-scaling-factor" ? fcitx::dbus::Variant(1.25) : fcitx::dbus::Variant(uint32_t(0));
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
        assert(current->textScaleKnown());
        if (changes == 1) {
            assert(current->textScale() == 1.25);
            for (const auto value : {std::numeric_limits<double>::quiet_NaN(), 10.0, 1.0}) {
                auto signal = portal.createSignal("/org/freedesktop/portal/desktop",
                    "org.freedesktop.portal.Settings", "SettingChanged");
                signal << std::string("org.gnome.desktop.interface") << std::string("text-scaling-factor") << fcitx::dbus::Variant(value);
                assert(signal.send());
            }
        } else {
            assert(current->textScale() == 1.0);
            loop.exit();
        }
    });
    current = &appearance;
    auto timeout = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 2000000, 0,
        [&](fcitx::EventSourceTime *, uint64_t) { loop.exit(); return false; });
    loop.exec();
    assert(changes == 2);
}
