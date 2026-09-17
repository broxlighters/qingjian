//! 真实私有 D-Bus/密封 memfd 回归：连续帧、乱序、曝光、重复点击与绘制丢失。
#include "panel/gnome/panel.h"
#include "qingjian_render.h"
#include "support/gnome_context.h"
#include <fcitx/instance.h>
#include <fcitx/focusgroup.h>
#include <fcitx-utils/unixfd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <unistd.h>
int main(int argc, char **argv) {
    const bool lostPaint = argc > 1 && std::string(argv[1]) == "lost-painted";
    const bool invalidBuffer = argc > 1 && std::string(argv[1]) == "buffer-invalid";
    char runtime[] = "/tmp/qingjian-gnome-XXXXXX";
    assert(mkdtemp(runtime));
    setenv("XDG_RUNTIME_DIR", runtime, 1);
    char program[] = "production-test", disabled[] = "--disable=all";
    char *arguments[] = {program, disabled, nullptr};
    fcitx::Instance instance(2, arguments);
    instance.initialize();
    fcitx::EventLoop loop;
    fcitx::dbus::Bus frontend(fcitx::dbus::BusType::Session), extension(fcitx::dbus::BusType::Session);
    frontend.attachEventLoop(&loop); extension.attachEventLoop(&loop);
    fcitx::FocusGroup group("wayland:", instance.inputContextManager());
    GnomeContext context(instance.inputContextManager(), &frontend);
    context.setFocusGroup(&group);
    context.setCapabilityFlags(fcitx::CapabilityFlags(fcitx::CapabilityFlag::Preedit) | fcitx::CapabilityFlag::RelativeRect);
    context.setCursorRect(fcitx::Rect(10, 10, 10, 30)); context.focusIn();
    assert(extension.requestName("org.qingjian.Panel1", fcitx::dbus::RequestNameFlag::None));
    unsigned hellos = 0, prepares = 0, shows = 0, actions = 0, acknowledgements = 0, hides = 0;
    uint64_t revision = 1;
    std::string firstToken;
    bool failed = false, started = false;
    const auto send = [&](const char *member, const std::string &token, uint32_t button = 4) {
        auto signal = extension.createSignal("/org/qingjian/Panel1", "org.qingjian.Panel1", member);
        signal << token;
        if (std::string(member) == "Pointer") signal << uint32_t(1) << uint32_t(1) << button;
        assert(signal.send());
    };
    auto object = extension.addObject("/org/qingjian/Panel1", [&](auto &message) {
        auto reply = message.createReply();
        const auto method = message.member();
        if (method == "Hello") {
            ++hellos;
            reply << std::string(R"({"version":1,"format":"RGBA_8888_PRE","length":5760000,"transport_epoch":"9007199254740993"})");
        } else if (method == "BindContext") {
            std::string token, source; message >> token >> source;
            auto outputs = extension.createSignal("/org/qingjian/Panel1", "org.qingjian.Panel1", "OutputsChanged");
            outputs << std::string("[[0,0,1920,1080]]");
            assert(outputs.send());
            auto identity = nlohmann::json::parse(token);
            assert(identity["transport_epoch"] == "9007199254740993");
            identity["geometry_revision"] = "9007199254740994";
            reply << nlohmann::json({{"identity", identity.dump()}, {"raster", 1.0},
                {"width", 1600}, {"height", 900}}).dump();
        } else if (method == "PrepareFrame") {
            ++prepares;
            if (invalidBuffer) {
                assert(message.createError("org.qingjian.Panel1.buffer_invalid", "buffer_invalid").send());
                return true;
            }
            std::string token, interactions;
            fcitx::UnixFD fd;
            uint32_t width, height, stride, length;
            double logicalWidth, logicalHeight;
            message >> token >> fd >> width >> height >> stride >> length >> logicalWidth >> logicalHeight >> interactions;
            assert(message && fd.fd() >= 0 && width * height * 4 == length && stride == width * 4);
            const auto regions = nlohmann::json::parse(interactions);
            assert(!regions.at("regions").empty() && regions.at("previous") == true && regions.at("next") == false);
            struct stat info{};
            assert(fstat(fd.fd(), &info) == 0 && info.st_size == length);
            const auto seals = fcntl(fd.fd(), F_GET_SEALS);
            assert((seals & (F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK)) == (F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK));
            assert(acknowledgements == prepares - 1);
            send("Released", token);
            send("Prepared", token);
        } else if (method == "Show") {
            std::string token; message >> token;
            ++shows;
            if (shows == 1) firstToken = token;
            if (!lostPaint) send("Painted", token);
            if (shows == 2) {
                assert(token != firstToken);
                send("Painted", firstToken); send("Pointer", firstToken);
                send("Pointer", token, 1); send("Pointer", token, 3); send("Pointer", token, 5);
                send("Pointer", token); send("Pointer", token);
            }
        } else if (method == "Hide") { ++hides; }
        else if (method != "Renew") return false;
        assert(reply.send());
        return true;
    });
    std::shared_ptr<QjRenderer> renderer(qj_renderer_create(QJ_RENDER_ABI_VERSION), qj_renderer_destroy);
    assert(renderer);
    const auto frame = nlohmann::json::parse(R"({"preedit":[],"cursor":0,"candidates":{"items":[{"text":"hello","kind":"Chinese","syllables":[],"reading":null,"translation":null}]},"highlight":0,"page":1,"page_count":2,"layout":"vertical","theme":"light","sentence":null,"notice":null})");
    const auto body = frame.dump();
    auto *warm = qj_renderer_render_sized(renderer.get(), QJ_RENDER_ABI_VERSION, QJ_RENDER_SIZE_ABI_VERSION,
        reinterpret_cast<const uint8_t *>(body.data()), body.size(), 1, 1, 1, 1600, 900, false);
    assert(warm); qj_result_destroy(warm);
    qingjian::panel::GnomePanel panel(loop);
    auto bridge = qingjian::panel::GnomeBridge::shared(loop);
    qingjian::panel::FocusIdentity focus;
    focus.key = qingjian::panel::KeyOrigin{":1.100", "/org/freedesktop/portal/inputcontext/1",
        "11111111111111111111111111111111", "ProcessKeyEvent", 1, 38, false};
    std::function<void()> render = [&] {
        const auto current = revision;
        assert(panel.render({context.fcitx::InputContext::watch(), renderer, frame, {1, focus.key->context, current}, focus,
            {}, 1, false, [&, current] { return revision == current; }, [&](int action) {
                assert(action == -1); ++actions; loop.exit();
            }, [&](const auto &) {
                ++acknowledgements;
                if (revision == 1 && !lostPaint) { ++revision; render(); }
            }, [&] { failed = true; loop.exit(); }, {}}) == qingjian::panel::Submission::Pending);
    };
    auto empty = frame;
    empty["candidates"]["items"] = nlohmann::json::array();
    assert(panel.render({context.fcitx::InputContext::watch(), renderer, empty, {1, focus.key->context, revision}, focus,
        {}, 1, false, [] { return true; }, {}, {}, [] { assert(false); }, {}}) == qingjian::panel::Submission::Accepted);
    assert(panel.state() == qingjian::panel::DisplayState::Idle && prepares == 0);
    // Hello 尚未分发时首帧有界等待，不能先曝光默认面板。
    assert(!bridge->ready() && bridge->negotiating());
    started = true;
    render();
    auto timeout = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 2000000, 0,
        [&](auto *, auto) { loop.exit(); return false; });
    loop.exec();
    assert(started && hellos == 1);
    assert(bridge->ready() && bridge->epoch() == "9007199254740993" && bridge->monitors().size() == 1);
    if (lostPaint || invalidBuffer) assert(failed && acknowledgements == 0 && actions == 0 && hides == 1);
    else assert(!failed && prepares == 2 && shows == 2 && acknowledgements == 2 && actions == 1 && hides == 0);
    if (invalidBuffer) {
        std::ifstream status(std::string(runtime) + "/qingjian-ui-" + std::to_string(getpid()) + ".json");
        nlohmann::json value; status >> value;
        assert(value.at("reason") == "buffer_invalid" && value.at("state") == "Fallback");
    }
    std::filesystem::remove_all(runtime);
}
