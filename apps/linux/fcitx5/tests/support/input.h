//! 可观测应用上屏与可重入 preedit 回调的真实 Fcitx 上下文。
#pragma once
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <functional>
class Input final : public fcitx::InputContext {
public:
    explicit Input(fcitx::InputContextManager &manager) : InputContext(manager, "real-test") {
        created();
        setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
        // 真实按键来自有焦点的输入框；失焦响应必须是空行内快照。
        focusIn();
    }
    ~Input() override { destroy(); }
    const char *frontend() const override { return "test"; }
    std::string committed;

    size_t commits = 0;

    std::function<void()> preeditHook;

    std::function<void()> commitHook;

    std::string preedit() const { return inputPanel().clientPreedit().toString(); }
protected:
    void commitStringImpl(const std::string &text) override {
        committed += text;
        ++commits;
        auto callback = std::move(commitHook);
        if (callback) callback();
    }
    void deleteSurroundingTextImpl(int, unsigned int) override {}
    void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
    void updatePreeditImpl() override { auto callback = std::move(preeditHook); if (callback) callback(); }
};
