//! 真 Server 多音节失焦：框架、只收到显示字符串的客户端和窗口提交均原样一次。
#include "qingjian.h"
#include "support/input.h"
#include "support/server.h"
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodentry.h>
#include <cassert>
#include <iostream>

int main(int argc, char **argv) {
    assert(argc == 3);
    const std::string mode = argv[1], owner = argv[2];
    Server server("preedit = \"" + mode + "\"\n");
    char program[] = "qingjian-focusout", disable[] = "--disable=all";
    char *arguments[] = {program, disable, nullptr};
    fcitx::Instance instance(2, arguments);
    instance.initialize();
    fcitx::QingjianEngine engine(&instance.addonManager());
    fcitx::InputMethodEntry entry("qingjian", "qingjian", "zh_CN", "qingjian");
    Input context(instance.inputContextManager());
    using Flag = fcitx::CapabilityFlag;
    fcitx::CapabilityFlags flags;
    if (owner != "no-inline") flags |= Flag::Preedit;
    if (owner == "client") flags |= Flag::ClientUnfocusCommit;
    context.setCapabilityFlags(flags);
    context.focusIn();
    for (char c : std::string("nihao"))
        assert(engine.process(&context, fcitx::Key(static_cast<fcitx::KeySym>(c))));
    const auto &panel = context.inputPanel();
    assert(panel.candidateList() && panel.candidateList()->candidate(0).text().toString() == "你好");
    const bool inlinePreedit = mode != "window" && owner != "no-inline";
    assert(panel.clientPreedit().empty() == !inlinePreedit);
    assert(panel.preedit().toString() == (mode != "inline" || !inlinePreedit ? "ni'hao" : ""));
    std::cerr << "失焦前：行内=" << context.preedit() << "，窗口=" << panel.preedit().toString() << '\n';
    // Wayland 等客户端只收到 toString() 的文本；不能借 toStringForCommit() 隐藏差异。
    if (owner == "client" && !context.preedit().empty()) context.commitString(context.preedit());
    context.focusOut();
    fcitx::FocusOutEvent out(&context);
    engine.deactivate(entry, out);
    std::cerr << "失焦后：文本=" << context.committed << "，提交次数=" << context.commits << '\n';
    assert(context.committed == "nihao" && context.commits == 1);
    assert(context.inputPanel().empty());
    // 重复失焦、Reset、回焦不能重放；重新输入只提交新的组句。
    engine.deactivate(entry, out);
    fcitx::InputContextEvent reset(&context, fcitx::EventType::InputContextReset);
    engine.reset(entry, reset);
    context.focusIn();
    assert(context.committed == "nihao" && context.commits == 1 && context.inputPanel().empty());
    for (char c : std::string("hao "))
        assert(engine.process(&context, fcitx::Key(static_cast<fcitx::KeySym>(c))));
    assert(context.committed == "nihao好" && context.commits == 2);
}
