//! 全局观察器不得清其他输入法回调；纯 Wayland 构建也须接线桌面设置。
#include "qingjian.h"
#include "support/context.h"
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx-utils/dbus/bus.h>
#include <fcitx-utils/dbus/variant.h>
#include <cassert>
#include <filesystem>

int main(int argc, char **) {
    char directory[] = "/tmp/qingjian-watchers-XXXXXX";
    assert(mkdtemp(directory));
    setenv("XDG_CONFIG_HOME", directory, 1);
    setenv("XDG_DATA_HOME", directory, 1);
    char program[] = "qingjian-watchers", disable[] = "--disable=all";
    char *arguments[] = {program, disable, nullptr};
    fcitx::Instance instance(2, arguments);
    instance.initialize();
    std::unique_ptr<fcitx::dbus::Bus> portal;
    std::unique_ptr<fcitx::dbus::Slot> settings;
    unsigned reads = 0;
    if (argc > 1) {
        portal = std::make_unique<fcitx::dbus::Bus>(fcitx::dbus::BusType::Session);
        portal->attachEventLoop(&instance.eventLoop());
        assert(portal->requestName("org.freedesktop.portal.Desktop", fcitx::dbus::RequestNameFlag::None));
        settings = portal->addObject("/org/freedesktop/portal/desktop", [&](fcitx::dbus::Message &message) {
            if (message.member() != "Read") return false;
            std::string space, key; message >> space >> key;
            auto reply = message.createReply();
            if (key == "text-scaling-factor") reply << fcitx::dbus::Variant(1.25);
            else reply << fcitx::dbus::Variant(uint32_t(0));
            ++reads;
            assert(reply.send());
            return true;
        });
    }
    fcitx::QingjianEngine engine(&instance.addonManager());
    Context context(instance.inputContextManager());
    bool called = false;
    context.inputPanel().setCustomInputPanelCallback([&](fcitx::InputContext *) { called = true; });
    context.setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
    assert(context.inputPanel().customInputPanelCallback());
    context.focusIn();
    assert(context.inputPanel().customInputPanelCallback());
    context.reset();
    assert(context.inputPanel().customInputPanelCallback());
    context.focusOut();
    assert(context.inputPanel().customInputPanelCallback());
    context.inputPanel().customInputPanelCallback()(&context);
    assert(called);
    if (argc > 1) {
        auto stop = instance.eventLoop().addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 100000, 1,
            [&](fcitx::EventSourceTime *, uint64_t) { instance.eventLoop().exit(); return false; });
        instance.eventLoop().exec();
        assert(reads == 2);
    }
    std::filesystem::remove_all(directory);
}
