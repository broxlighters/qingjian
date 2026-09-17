//! 私有 D-Bus 假扩展在绘制后丢弃续约，应在有限时间回退，不能无限取消请求期限。
#include "panel/gnome/probe.h"
#include "qingjian_render.h"
#include "support/gnome_context.h"
#include <fcitx/instance.h>
#include <fcitx/focusgroup.h>
#include <fcitx/inputcontextmanager.h>
#include <cassert>
#include <chrono>
#include <cstdlib>
int main(int argc, char **argv) {
    const bool geometryLoop = argc > 1 && std::string(argv[1]) == "geometry-loop";
    const bool geometryBeforePaint = argc > 1 && std::string(argv[1]) == "geometry-prepare";
    const bool geometry = geometryBeforePaint || (argc > 1 && std::string(argv[1]) == "geometry");
    const bool losePainted = argc > 1 && std::string(argv[1]) == "lost-painted";
    const bool loseValidDuringPrepare = argc > 1 && std::string(argv[1]) == "invalid-prepare";
    setenv("QINGJIAN_GNOME_PROBE", "1", 1);
    char program[] = "probe-test", disabled[] = "--disable=all";
    char *arguments[] = {program, disabled, nullptr};
    fcitx::Instance instance(2, arguments);
    instance.initialize();
    fcitx::EventLoop loop;
    fcitx::dbus::Bus frontend(fcitx::dbus::BusType::Session);
    frontend.attachEventLoop(&loop);
    fcitx::FocusGroup group("wayland:", instance.inputContextManager());
    GnomeContext context(instance.inputContextManager(), &frontend);
    context.setFocusGroup(&group);
    context.setCapabilityFlags(fcitx::CapabilityFlags(fcitx::CapabilityFlag::Preedit) | fcitx::CapabilityFlag::RelativeRect);
    context.setCursorRect(fcitx::Rect(10, 10, 10, 30));
    context.focusIn();
    fcitx::dbus::Bus extension(fcitx::dbus::BusType::Session);
    extension.attachEventLoop(&loop);
    assert(extension.requestName("org.qingjian.PanelProbe1", fcitx::dbus::RequestNameFlag::None));
    unsigned renews = 0, shows = 0, prepares = 0, actions = 0, acknowledgements = 0;
    std::string firstToken;
    bool painted = false, fallback = false, valid = true;
    const auto send = [&](const char *member, const std::string &id) {
        auto event = extension.createSignal("/org/qingjian/PanelProbe1", "org.qingjian.PanelProbe1", member);
        event << id;
        if (std::string(member) == "Pointer") event << uint32_t(10) << uint32_t(10) << uint32_t(4);
        assert(event.send());
    };
    auto object = extension.addObject("/org/qingjian/PanelProbe1", [&](fcitx::dbus::Message &message) {
        auto reply = message.createReply();
        if (message.member() == "Hello") reply << uint32_t(2);
        else if (message.member() == "Bind") reply << 1.0 << uint32_t(1600) << uint32_t(900);
        else if (message.member() == "Prepare") {
            reply << uint32_t(1);
            ++prepares;
            if (geometryLoop || (geometryBeforePaint && prepares == 1)) {
                message >> firstToken;
                send("GeometryChanged", firstToken);
                send("Pointer", firstToken);
                send("Painted", firstToken);
                send("GeometryChanged", firstToken);
                auto error = message.createError("org.qingjian.PanelProbe1.BufferInvalid", "stale");
                assert(error.send());
                return true;
            }
            if (loseValidDuringPrepare) valid = false;
        }
        else if (message.member() == "Show") {
            std::string token; message >> token;
            auto signal = extension.createSignal("/org/qingjian/PanelProbe1", "org.qingjian.PanelProbe1", "Painted");
            signal << token;
            if (!losePainted) assert(signal.send());
            painted = true;
            if (geometry) {
                ++shows;
                if (shows == 1 && !geometryBeforePaint) {
                    firstToken = token;
                    send("GeometryChanged", token);
                    send("Pointer", token);
                    send("GeometryChanged", token);
                } else {
                    assert(shows == (geometryBeforePaint ? 1U : 2U) && token != firstToken);
                    send("Painted", firstToken);
                    send("Pointer", firstToken);
                    send("GeometryChanged", firstToken);
                    send("Pointer", token);
                }
            }
        } else if (message.member() == "Renew") { ++renews; return true; }
        else if (message.member() == "Hide" && painted) return true; // 连撤窗也不应答，必须等租约后回退。
        else if (message.member() != "Hide" && message.member() != "Withdraw") return false;
        assert(reply.send());
        return true;
    });
    std::shared_ptr<QjRenderer> renderer(qj_renderer_create(QJ_RENDER_ABI_VERSION), qj_renderer_destroy);
    assert(renderer);
    qingjian::panel::GnomeProbe probe(loop);
    qingjian::panel::FocusIdentity identity;
    identity.key = qingjian::panel::KeyOrigin{":1.100", "/org/freedesktop/portal/inputcontext/1",
        "11111111111111111111111111111111", "ProcessKeyEvent", 1, 38, false};
    const auto frame = nlohmann::json::parse(R"({"preedit":[],"cursor":0,"candidates":{"items":[{"text":"hello","kind":"Chinese","syllables":[],"reading":null,"translation":null}]},"highlight":0,"page":1,"page_count":2,"layout":"vertical","theme":"light","sentence":null,"notice":null})");
    // 这些用例检查协议期限/重排，先预热固定帧的字形；桌面冷首帧另行实测。
    const auto body = frame.dump();
    auto *warm = qj_renderer_render_sized(renderer.get(), QJ_RENDER_ABI_VERSION, QJ_RENDER_SIZE_ABI_VERSION,
        reinterpret_cast<const uint8_t *>(body.data()), body.size(), 1, 1, 1, 1600, 900, false);
    assert(warm);
    qj_result_destroy(warm);
    const auto started = std::chrono::steady_clock::now();
    assert(probe.render(&context, renderer, frame, {}, 1.0, false, [&] { return valid; }, [&](int value) {
            assert(geometry && value == -1); ++actions; loop.exit();
        },
        [&](const nlohmann::json &) { ++acknowledgements; }, [&] { fallback = true; loop.exit(); }, identity));
    auto stop = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + (loseValidDuringPrepare ? 500000 : 2000000), 1,
        [&](fcitx::EventSourceTime *, uint64_t) { loop.exit(); return false; });
    loop.exec();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    if (geometryLoop) {
        assert(prepares > 1 && shows == 0 && acknowledgements == 0 && fallback);
        assert(elapsed >= std::chrono::milliseconds(200) && elapsed < std::chrono::milliseconds(700));
        return 0;
    }
    if (geometry) {
        assert(shows == (geometryBeforePaint ? 1U : 2U) && actions == 1 && acknowledgements == shows && !fallback);
        assert(prepares == 2);
        return 0;
    }
    if (loseValidDuringPrepare) { assert(!painted && renews == 0 && !valid); return 0; }
    assert(painted && renews == (losePainted ? 0U : 1U) && fallback);
    assert(elapsed >= std::chrono::milliseconds(700));
    assert(elapsed < std::chrono::milliseconds(1500));
}
