//! 几何改变自动重绘当前有效帧，作废排队点击且不曝光默认面板。
#include "panel/controller.h"
#include "support/context.h"
#include "support/memory.h"
#include <fcitx/instance.h>
#include <fcitx/focusgroup.h>
#include <cassert>
#include <chrono>
#include <thread>
#include <fstream>
#include <filesystem>
#include <unistd.h>
int main() {
    char runtime[] = "/tmp/qingjian-geometry-XXXXXX";
    assert(mkdtemp(runtime));
    setenv("XDG_RUNTIME_DIR", runtime, 1);
    char program[] = "geometry-test", disabled[] = "--disable=all";
    char *arguments[] = {program, disabled, nullptr};
    fcitx::Instance instance(2, arguments);
    instance.initialize();
    fcitx::EventLoop loop;
    fcitx::FocusGroup group("x11:memory-test", instance.inputContextManager());
    Context context(instance.inputContextManager());
    context.setFocusGroup(&group);
    context.setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
    context.setCursorRect(fcitx::Rect(10, 10, 11, 30));
    context.focusIn();
    auto backend = std::make_unique<MemoryBackend>();
    auto *memory = backend.get();
    qingjian::panel::Controller panel(std::move(backend));
    panel.configure(qingjian::panel::RendererMode::Qingjian, &loop);
    const auto frame = nlohmann::json::parse(R"({"preedit":[],"cursor":0,"candidates":{"items":[{"text":"hello","kind":"Chinese","syllables":[],"reading":null,"translation":null}]},"highlight":0,"page":0,"page_count":1,"layout":"vertical","theme":"light","sentence":null,"notice":null})");
    const qingjian::panel::FrameIdentity identity{1, "test", 1};
    unsigned recoveries = 0, acknowledgements = 0;
    std::function<bool()> render = [&] {
        return panel.renderFrame(&context, frame, identity, [&] { return context.hasFocus(); }, [](int) { assert(false); },
            [&](const auto &) { ++acknowledgements; }, [] { assert(false); }, std::chrono::steady_clock::now(),
            false, 1.0, false, {}, [&] {
                ++recoveries;
                assert(!panel.accepts(identity) && !memory->shown);
                assert(render());
                panel.refresh(&context);
                loop.exit();
            });
    };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!render() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    panel.refresh(&context);
    assert(panel.accepts(identity) && acknowledgements == 1);
    memory->changed = true;
    memory->enqueue({40, 20, 1});
    memory->enqueue({40, 20, 1});
    auto timeout = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 1000000, 0,
        [&](auto *, auto) { loop.exit(); return false; });
    loop.exec();
    assert(recoveries == 1 && acknowledgements == 2 && panel.accepts(identity) && memory->shown);
    const auto status = [&] {
        std::ifstream stream(std::string(runtime) + "/qingjian-ui-" + std::to_string(getpid()) + ".json");
        nlohmann::json value; stream >> value; return value;
    };
    assert(status().at("state") == "Visible");
    panel.hide(&context);
    assert(status().at("state") == "Idle" && status().at("painted") == false);
    panel.configure(qingjian::panel::RendererMode::Qingjian, &instance.eventLoop());
    assert(render()); panel.refresh(&context);
    context.focusOut(); panel.refresh(&context);
    assert(status().at("state") == "Idle" && status().at("painted") == false);
    context.focusIn();
    assert(render()); panel.refresh(&context);
    auto empty = frame;
    empty["candidates"]["items"] = nlohmann::json::array();
    assert(!panel.renderFrame(&context, empty, identity, [] { return true; }, {}, {}, {}, std::chrono::steady_clock::now()));
    assert(status().at("state") == "Idle" && status().at("painted") == false);
    std::filesystem::remove_all(runtime);
}
