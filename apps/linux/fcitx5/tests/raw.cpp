//! 实际 Server 的原样输入矩阵：行内客户端提交和仅窗口 Server 提交结果相同。
#include "qingjian.h"
#include "support/input.h"
#include "support/server.h"
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodentry.h>
#include <cassert>
#include <iostream>

int main(int argc, char **argv) {
    assert(argc == 3);
    const std::string scenario = argv[1], mode = argv[2];
    std::string config = "preedit = \"" + mode + "\"\nshift_letter = \"compose\"\n";
    if (scenario == "shuangpin") config += "scheme = \"xiaohe\"\n";
    if (scenario.rfind("zhuyin", 0) == 0) config += "scheme = \"zhuyin\"\n";
    Server server(config, scenario == "correction" ? "你好吗\tni hao ma\t50000\n" : "");
    char program[] = "qingjian-raw", disable[] = "--disable=all";
    char *arguments[] = {program, disable, nullptr};
    fcitx::Instance instance(2, arguments);
    instance.initialize();
    fcitx::QingjianEngine engine(&instance.addonManager());
    fcitx::InputMethodEntry entry("qingjian", "qingjian", "zh_CN", "qingjian");
    Input context(instance.inputContextManager());
    auto caps = fcitx::CapabilityFlags(fcitx::CapabilityFlag::Preedit) | fcitx::CapabilityFlag::ClientUnfocusCommit;
    if (scenario == "sensitive") caps |= fcitx::CapabilityFlag::Sensitive;
    context.setCapabilityFlags(caps);
    context.focusIn();
    auto press = [&](fcitx::KeySym key) { assert(engine.process(&context, fcitx::Key(key))); };
    auto type = [&](const std::string &text) {
        for (char c : text) {
            const auto modifiers = c >= 'A' && c <= 'Z' ? fcitx::KeyStates(fcitx::KeyState::Shift) : fcitx::KeyStates();
            assert(engine.process(&context, fcitx::Key(static_cast<fcitx::KeySym>(c), modifiers)));
        }
    };
    std::string keys = "nihao", expected = "nihao", prefix;
    int cursor = 5;
    if (scenario == "explicit") { keys = expected = "xi'an"; }
    if (scenario == "case") { keys = expected = "NiHao"; }
    if (scenario == "correction") { keys = expected = "nihooma"; cursor = 7; }
    if (scenario == "shuangpin") { keys = expected = "nihc"; cursor = 4; }
    if (scenario == "zhuyin") { keys = "1j41j4"; expected = "ㄅㄨˋㄅㄨˋ"; cursor = 16; }
    if (scenario == "zhuyin-light") { keys = "7su"; expected = "ㄋㄧ˙"; cursor = 8; }
    if (scenario == "zhuyin-explicit") { keys = "su'1j4"; expected = "ㄋㄧ'ㄅㄨˋ"; cursor = 15; }
    if (scenario == "zhuyin-incomplete") { keys = "su"; expected = "ㄋㄧ"; cursor = 6; }
    if (scenario == "english") {
        engine.process(&context, fcitx::Key(FcitxKey_Shift_L));
        assert(engine.process(&context, fcitx::Key(FcitxKey_Shift_L), true));
        keys = expected = "hello";
    }
    if (scenario == "raw") { keys = expected = "gpt-6"; }
    if (scenario == "partial") { keys = "kaifazhe"; expected = "zhe"; cursor = 3; }
    if (scenario == "empty") { keys = expected = ""; cursor = 0; }
    type(keys);
    if (scenario == "middle") {
        press(FcitxKey_Home); press(FcitxKey_Right); press(FcitxKey_Right);
        type("x"); press(FcitxKey_Delete); expected = "nixao"; cursor = 3;
    }
    if (scenario == "home") { press(FcitxKey_Home); cursor = 0; }
    if (scenario == "end") { press(FcitxKey_Home); press(FcitxKey_End); }
    if (scenario == "zhuyin-light") { press(FcitxKey_Home); press(FcitxKey_Right); cursor = 3; }
    if (scenario == "zhuyin") {
        press(FcitxKey_Left); press(FcitxKey_Left); press(FcitxKey_Left);
        press(FcitxKey_BackSpace); type("6"); expected = "ㄅㄨˊㄅㄨˋ"; cursor = 8;
    }
    if (scenario == "partial") {
        auto list = context.inputPanel().candidateList();
        assert(list && list->candidate(0).text().toString() == "开发");
        list->candidate(0).select(&context);
        prefix = "开发";
        assert(context.committed == prefix && context.commits == 1);
    }
    if (scenario == "password" || scenario == "disabled") {
        context.setCapabilityFlags(scenario == "password" ? fcitx::CapabilityFlag::Password : fcitx::CapabilityFlag::Disable);
        expected = ""; cursor = 0;
    }
    std::cerr << scenario << "：行内=" << context.preedit() << "，窗口=" << context.inputPanel().preedit().toString() << '\n';
    assert(context.preedit() == (mode == "window" ? "" : expected));
    if (mode != "window" && !expected.empty()) assert(context.inputPanel().clientPreedit().cursor() == cursor);
    if (scenario == "shuangpin") assert(context.inputPanel().preedit().toString() == "ni'hao");
    if (scenario == "correction") assert(context.inputPanel().preedit().toString() != expected);
    if (!context.preedit().empty()) context.commitString(context.preedit());
    context.focusOut();
    fcitx::FocusOutEvent out(&context);
    engine.deactivate(entry, out);
    const auto commits = size_t(!prefix.empty()) + size_t(!expected.empty());
    assert(context.committed == prefix + expected && context.commits == commits);
    assert(context.inputPanel().empty());
    engine.deactivate(entry, out);
    fcitx::InputContextEvent reset(&context, fcitx::EventType::InputContextReset);
    engine.reset(entry, reset);
    assert(context.committed == prefix + expected && context.commits == commits);
}
