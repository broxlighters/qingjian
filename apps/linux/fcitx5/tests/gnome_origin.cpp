//! 私有总线加载系统真实 dbusfrontend，验证主/portal 调用来源及调用栈外拒绝。
#include "panel/gnome/identity/key_origin.h"
#include "support/keyboard/factory.h"
#include <fcitx-utils/dbus/bus.h>
#include <fcitx-utils/event.h>
#include <fcitx/instance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/addonloader.h>
#include <fcitx/inputcontextmanager.h>
#include <atomic>
#include <cassert>
#include <filesystem>
#include <thread>
#include <vector>

int main() {
    char directory[] = "/tmp/qingjian-origin-XXXXXX";
    assert(mkdtemp(directory));
    setenv("XDG_CONFIG_HOME", directory, 1);
    setenv("XDG_DATA_HOME", directory, 1);
    unsetenv("DISPLAY");
    unsetenv("WAYLAND_DISPLAY");
    char name[] = "qingjian-origin", disable[] = "--disable=all";
    char enable[] = "--enable=keyboard,dbus,dbusfrontend", ui[] = "--ui=none";
    char *arguments[] = {name, disable, enable, ui, nullptr};
    fcitx::Instance instance(4, arguments);
    KeyboardFactory keyboard;
    fcitx::StaticAddonRegistry registry{{"keyboard", &keyboard}};
    instance.addonManager().registerDefaultLoader(&registry);
    instance.initialize();
    fcitx::dbus::Bus client(fcitx::dbus::BusType::Session);
    fcitx::dbus::Bus stranger(fcitx::dbus::BusType::Session);
    const auto sender = client.uniqueName();
    // 只给本测试已加载的连接发消息；GetNameOwner 不会触发 service activation。
    const std::vector<std::string> services{
        client.serviceOwner("org.fcitx.Fcitx5", 1000000),
        client.serviceOwner("org.freedesktop.portal.Fcitx", 1000000)};
    assert(!services[0].empty() && !services[1].empty());
    const char *interface = "org.fcitx.Fcitx.InputContext1";
    std::vector<qingjian::panel::KeyOrigin> origins;
    auto watcher = instance.watchEvent(fcitx::EventType::InputContextKeyEvent,
        fcitx::EventWatcherPhase::PreInputMethod, [&](fcitx::Event &event) {
            auto &key = static_cast<fcitx::KeyEvent &>(event);
            auto origin = qingjian::panel::KeyOrigin::capture(key);
            assert(origin && origin->sender == sender && origin->code == 38);
            assert(origin->context.size() == 32 && origin->path.starts_with("/org/freedesktop/portal/inputcontext/"));
            origins.push_back(*origin);
            key.filterAndAccept();
        });
    std::atomic<bool> done = false;
    std::vector<std::string> contexts;
    std::thread worker([&] {
        for (const auto &serviceName : services) {
            const auto *service = serviceName.c_str();
            auto create = client.createMethodCall(service, "/org/freedesktop/portal/inputmethod",
                "org.fcitx.Fcitx.InputMethod1", "CreateInputContext");
            create << std::vector<fcitx::dbus::DBusStruct<std::string, std::string>>{{"program", "not-the-window-class"}};
            auto created = create.call(1000000);
            assert(created && !created.isError());
            fcitx::dbus::ObjectPath path;
            std::vector<uint8_t> uuid;
            created >> path >> uuid;
            assert(created && uuid.size() == 16);
            std::ostringstream hex;
            hex << std::hex << std::setfill('0');
            for (auto byte : uuid) hex << std::setw(2) << static_cast<unsigned>(byte);
            contexts.push_back(hex.str());
            auto focus = client.createMethodCall(service, path.path().c_str(), interface, "FocusIn").call(1000000);
            assert(focus && !focus.isError());
            for (const auto *method : {"ProcessKeyEvent", "ProcessKeyEventBatch"}) {
                auto key = client.createMethodCall(service, path.path().c_str(), interface, method);
                const bool release = std::string(method) == "ProcessKeyEventBatch";
                key << uint32_t(FcitxKey_a) << uint32_t(38) << uint32_t(0) << release
                    << (release ? uint32_t(1) : uint32_t(0xfffffff0));
                auto result = key.call(1000000);
                assert(result && !result.isError());
            }
            // 相同路径、参数但错误 unique sender 不得进入真实 KeyEvent。
            auto forged = stranger.createMethodCall(service, path.path().c_str(), interface, "ProcessKeyEvent");
            forged << uint32_t(FcitxKey_a) << uint32_t(38) << uint32_t(0) << false << uint32_t(2);
            auto rejected = forged.call(1000000);
            bool accepted = true;
            rejected >> accepted;
            assert(rejected && !accepted);
            auto wrong = client.createMethodCall(service, path.path().c_str(), "org.qingjian.Invalid", "ProcessKeyEvent");
            wrong << uint32_t(FcitxKey_a) << uint32_t(38) << uint32_t(0) << false << uint32_t(2);
            assert(wrong.call(1000000).isError());
        }
        done.store(true);
    });
    auto timer = instance.eventLoop().addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC), 1,
        [&](fcitx::EventSourceTime *timer, uint64_t now) {
            if (!done.load()) {
                timer->setTime(now + 10000);
                timer->setEnabled(true);
                return true;
            }
            // 仍然存在的同一 IC，离开注册方法调用栈后不能沿用旧 currentMessage。
            instance.inputContextManager().foreach([](fcitx::InputContext *context) {
                fcitx::KeyEvent synthetic(context, fcitx::Key(FcitxKey_a));
                assert(!qingjian::panel::KeyOrigin::capture(synthetic));
                return true;
            });
            instance.eventLoop().exit();
            return false;
        });
    instance.eventLoop().exec();
    worker.join();
    assert(origins.size() == 4 && contexts.size() == 2 && contexts[0] != contexts[1]);
    for (size_t i = 0; i < origins.size(); ++i) {
        assert(origins[i].context == contexts[i / 2]);
        assert(origins[i].release == bool(i % 2));
        assert(origins[i].time == (i % 2 ? uint32_t(1) : uint32_t(0xfffffff0)));
    }
    std::filesystem::remove_all(directory);
}
