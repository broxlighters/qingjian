//! 真实 Rust 位图配合无窗口承载，验证回调提交、失焦、旧帧、失败回退与结果寿命。
#include "panel/controller.h"
#include "qingjian_render.h"
#include "support/context.h"
#include "support/memory.h"
#include <fcitx/instance.h>
#include <fcitx/inputcontext.h>
#include <fcitx/focusgroup.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <cassert>
#include <chrono>
#include <thread>
#include <vector>
#include <array>
int main() {
    char program[] = "qingjian-controller";
    char disabled[] = "--disable=all";
    char *arguments[] = {program, disabled, nullptr};
    fcitx::Instance instance(2, arguments);
    instance.initialize();
    fcitx::EventLoop healthLoop;
    fcitx::FocusGroup group("x11:memory-test", instance.inputContextManager());
    Context context(instance.inputContextManager());
    context.setFocusGroup(&group);
    context.setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
    context.setCursorRect(fcitx::Rect(100, 100, 101, 120));
    context.focusIn();
    auto backend = std::make_unique<MemoryBackend>();
    auto *memory = backend.get();
    qingjian::panel::Controller controller(std::move(backend));
    controller.configure(qingjian::panel::RendererMode::Qingjian, &instance.eventLoop());
    assert(controller.eligible(&context));
    const auto frame = nlohmann::json::parse(R"({"preedit":[{"text":"nihao","kind":"Typed"}],"cursor":5,"candidates":{"items":[{"text":"hello","kind":"Chinese","syllables":[],"reading":null,"translation":{"language":"English","senses":[{"text":"greeting","part_of_speech":null,"reading":null,"fresh":true}]}}]},"highlight":0,"page":0,"page_count":2,"layout":"vertical","theme":"light","sentence":null,"notice":null})");
    bool valid = true;
    unsigned acknowledgments = 0, fallbacks = 0;
    const qingjian::panel::FrameIdentity id{2, "context", 4};
    auto render = [&] {
        return controller.renderFrame(&context, frame, id, [&] { return valid; }, [](int) {},
            [&](const nlohmann::json &senses) { assert(senses == nlohmann::json::parse("[[0,0]]")); ++acknowledgments; },
            [&] { ++fallbacks; }, std::chrono::steady_clock::now());
    };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!render() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(context.inputPanel().customInputPanelCallback());
    assert(!memory->shown && acknowledgments == 0); // 注册回调不算实际呈现。
    context.inputPanel().customInputPanelCallback()(&context);
    assert(memory->shown && acknowledgments == 1 && controller.accepts(id));
    assert(!controller.accepts({2, "context", 5}));
    // 连续有效帧保留映射，仅作废旧动作；不得触发unmap/map长尾。
    const auto hidesBeforeUpdate = memory->hides;
    const auto ackBeforeUpdate = acknowledgments;
    assert(controller.canUpdate(&context) && render());
    assert(memory->shown && memory->hides == hidesBeforeUpdate && !controller.active());
    assert(!controller.accepts(id));
    controller.refresh(&context);
    assert(controller.active() && memory->hides == hidesBeforeUpdate && acknowledgments == ackBeforeUpdate + 1);
    // 更新期间非法布局失败必须撤下旧窗口和callback。
    auto brokenFrame = frame;
    brokenFrame["layout"] = "invalid";
    assert(!controller.renderFrame(&context, brokenFrame, id, [&] { return valid; }, [](int) {},
        [](const nlohmann::json &) {}, [] {}, std::chrono::steady_clock::now()));
    assert(!memory->shown && !context.inputPanel().customInputPanelCallback());
    assert(render());
    controller.refresh(&context);
    valid = false;
    controller.refresh(&context);
    assert(!memory->shown && !controller.active() && acknowledgments == ackBeforeUpdate + 2);
    valid = true;
    assert(render());
    memory->fail = true;
    controller.refresh(&context);
    assert(!memory->shown && fallbacks == 1 && !context.inputPanel().customInputPanelCallback());
    memory->fail = false;
    assert(render());
    memory->throwing = true;
    controller.refresh(&context);
    assert(!memory->shown && fallbacks == 2 && !context.inputPanel().customInputPanelCallback());
    memory->throwing = false;
    for (auto flag : {fcitx::CapabilityFlag::Disable, fcitx::CapabilityFlag::Password, fcitx::CapabilityFlag::Sensitive}) {
        context.setCapabilityFlags(flag);
        assert(!controller.eligible(&context) && !render());
    }
    context.setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
    assert(render());
    controller.refresh(&context);
    assert(memory->shown);
    context.focusOut();
    controller.refresh(&context);
    assert(!memory->shown && !controller.active());
    controller.invalidate(&context);
    assert(!controller.accepts(id));

    // 无 X 事件的合成器丢失也要同步隐藏、解除 callback，再刷新默认 UI。
    context.focusIn();
    controller.configure(qingjian::panel::RendererMode::Qingjian, &healthLoop);
    assert(render());
    controller.refresh(&context);
    memory->healthyConnection = false;
    const auto priorFallbacks = fallbacks;
    auto stop = healthLoop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 600000, 0,
        [&](fcitx::EventSourceTime *, uint64_t) { healthLoop.exit(); return false; });
    healthLoop.exec();
    assert(fallbacks == priorFallbacks + 1 && !memory->shown && !controller.active());
    assert(!context.inputPanel().customInputPanelCallback());
    memory->healthyConnection = true;
    controller.configure(qingjian::panel::RendererMode::Qingjian, &instance.eventLoop());
    assert(render());
    controller.refresh(&context);
    fcitx::FocusGroup wayland("wayland:probe", instance.inputContextManager());
    context.setFocusGroup(&wayland);
    context.focusIn();
    controller.refresh(&context);
    assert(!controller.active() && !memory->shown && !controller.eligible(&context));
    context.setFocusGroup(&group);
    context.focusIn();
    controller.configure(qingjian::panel::RendererMode::Auto, &instance.eventLoop());
    assert(!controller.eligible(&context) && !render());
    controller.configure(qingjian::panel::RendererMode::Qingjian, &instance.eventLoop());

    // 经真实 Rust 命中区域和 Fcitx fd 分发验证鼠标路径，不暴露 Controller 私有方法。
    context.focusIn();
    auto sparse = frame;
    sparse["candidates"]["items"].push_back(frame["candidates"]["items"][0]);
    sparse["candidates"]["items"][1]["text"] = "";
    sparse["candidates"]["items"].push_back(frame["candidates"]["items"][0]);
    sparse["page"] = 1;
    sparse["page_count"] = 3;
    const auto body = sparse.dump();
    std::unique_ptr<QjRenderer, decltype(&qj_renderer_destroy)> renderer(qj_renderer_create(QJ_RENDER_ABI_VERSION), qj_renderer_destroy);
    std::unique_ptr<QjResult, decltype(&qj_result_destroy)> result(qj_renderer_render(renderer.get(), QJ_RENDER_ABI_VERSION,
        reinterpret_cast<const uint8_t *>(body.data()), body.size(), context.scaleFactor(), 1600, 900, 0), qj_result_destroy);
    QjImageInfo image{};
    assert(result && qj_result_image(result.get(), &image));
    auto locate = [&](int target, bool page) {
        for (uint32_t y = 0; y < image.height; ++y)
            for (uint32_t x = 0; x < image.width; ++x)
                if ((page ? qj_result_page(result.get(), x, y) : qj_result_hit(result.get(), x, y)) == target)
                    return std::array<int, 3>{int(x), int(y), 1};
        assert(false);
        return std::array<int, 3>{};
    };
    const auto first = locate(0, false), third = locate(2, false);
    const auto previous = locate(-1, true), next = locate(1, true);
    const auto empty = std::array<int, 3>{first[0], (first[1] + third[1]) / 2, 1};
    assert(qj_result_hit(result.get(), empty[0], empty[1]) == -1);
    for (unsigned scenario = 0; scenario < 8; ++scenario) {
        fcitx::EventLoop loop;
        auto testBackend = std::make_unique<MemoryBackend>();
        auto *input = testBackend.get();
        qingjian::panel::Controller panel(std::move(testBackend));
        panel.configure(qingjian::panel::RendererMode::Qingjian, &loop);
        std::vector<int> actions;
        valid = true;
        assert(panel.renderFrame(&context, sparse, id, [&] { return valid; }, [&](int action) {
            actions.push_back(action);
            panel.hide(&context); // 同步提交/换页时，当前回调及结果可被撤销。
            if (scenario == 7) {
                // 同一 Server identity 的缩放/重绘也不能收到旧帧剩余点击。
                assert(panel.renderFrame(&context, sparse, id, [&] { return valid; },
                    [&](int) { assert(false); }, [](const nlohmann::json &) {}, [] { assert(false); },
                    std::chrono::steady_clock::now()));
                panel.refresh(&context);
            }
        }, [](const nlohmann::json &) {}, [] { assert(false); }, std::chrono::steady_clock::now()));
        panel.refresh(&context);
        assert(panel.active());
        if (scenario == 0 || scenario == 7) {
            input->enqueue({0, 0, 1}); // 阴影和空槽之后的有效事件不能滞留。
            input->enqueue(empty);
            input->enqueue(third);
            input->enqueue(first); // 选词后的旧帧事件必须丢弃。
        } else if (scenario < 5) {
            input->enqueue(scenario == 1 ? previous : scenario == 2 ? next :
                std::array<int, 3>{first[0], first[1], scenario == 3 ? 4 : 5});
        } else {
            if (scenario == 5) valid = false;
            input->enqueue(scenario == 5 ? first : std::array<int, 3>{first[0], first[1], 3});
        }
        bool processed = false;
        input->afterPoll = [&] { processed = true; loop.exit(); };
        auto timeout = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 1000000, 0,
            [&](fcitx::EventSourceTime *, uint64_t) { loop.exit(); return false; });
        loop.exec();
        assert(processed);
        const std::vector<int> expected = scenario == 5 || scenario == 6 ? std::vector<int>{} :
            std::vector<int>{scenario == 0 || scenario == 7 ? 2 : (scenario == 1 || scenario == 3 ? -1 : -2)};
        assert(actions == expected);
        if (scenario == 7) assert(panel.active() && panel.accepts(id));
        panel.hide(&context);
    }
}
