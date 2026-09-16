//! PanelController 的无桌面能力门控和帧身份回归。
#include "panel/controller.h"
#include "panel/backend/placement.h"
#include "panel/backend/area.h"
#include "panel/interaction/action.h"

#include <fcitx/instance.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <cassert>

class Context final : public fcitx::InputContext {
public:
    explicit Context(fcitx::InputContextManager &manager)
        : InputContext(manager, "panel-test") {
        created();
    }
    ~Context() override { destroy(); }
    const char *frontend() const override { return "test"; }
protected:
    void commitStringImpl(const std::string &) override {}
    void deleteSurroundingTextImpl(int, unsigned int) override {}
    void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
    void updatePreeditImpl() override {}
};

int main() {
    using qingjian::panel::FrameIdentity;
    using qingjian::panel::RendererMode;
    using qingjian::panel::x11Display;
    assert(x11Display("x11::0"));
    assert(x11Display("x11:display"));
    assert(!x11Display("wayland:wayland-0"));
    assert(!x11Display(""));
    FrameIdentity first{3, "context", 8};
    FrameIdentity second{3, "context", 9};
    assert(first != second);
    qingjian::panel::Controller controller;
    assert(!controller.active());
    assert(!controller.accepts(first));
    char program[] = "qingjian-panel";
    char *arguments[] = {program, nullptr};
    fcitx::Instance instance(1, arguments);
    Context context(instance.inputContextManager());
    controller.registerCallback(&context);
    assert(!context.inputPanel().customInputPanelCallback());
    controller.hide(&context);
    assert(!context.inputPanel().customInputPanelCallback());
    controller.configure(RendererMode::Auto, nullptr);
    assert(!controller.eligible(&context));
    const fcitx::Rect screen(0, 0, 1920, 1080);
    auto bottomRight = qingjian::panel::place(screen, fcitx::Rect(1910, 1060, 1911, 1080), 300, 100);
    assert(bottomRight.first == 1620 && bottomRight.second == 960);
    auto negative = qingjian::panel::place(fcitx::Rect(-1920, 0, 0, 1080), fcitx::Rect(-1900, 10, -1899, 30), 300, 100);
    assert(negative.first == -1900 && negative.second == 30);
    const std::vector<fcitx::Rect> monitors{fcitx::Rect(-1920, 0, 0, 1080), fcitx::Rect(0, 200, 2560, 1640)};
    const auto work = fcitx::Rect(-1920, 30, 2560, 1600);
    auto left = qingjian::panel::availableArea(monitors, work, fcitx::Rect(-20, 100, -19, 120));
    assert(left == fcitx::Rect(-1920, 30, 0, 1080));
    auto right = qingjian::panel::availableArea(monitors, work, fcitx::Rect(10, 300, 11, 320));
    assert(right == fcitx::Rect(0, 200, 2560, 1600));
    auto gap = qingjian::panel::availableArea(monitors, std::nullopt, fcitx::Rect(500, 100, 501, 120));
    assert(gap == monitors[1]);
    assert(qingjian::panel::availableArea({}, work, fcitx::Rect()).isEmpty());
    assert(!qingjian::panel::action(1, -1, true, true));
    assert(qingjian::panel::action(1, 2, false, false) == 2);
    assert(!qingjian::panel::action(4, 0, false, true));
    assert(qingjian::panel::action(5, 0, false, true) == -2);
    return 0;
}
