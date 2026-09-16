//! 控制器回归使用的无应用输入上下文。
#pragma once
#include <fcitx/inputcontext.h>
class Context final : public fcitx::InputContext {
public:
    explicit Context(fcitx::InputContextManager &manager) : InputContext(manager, "panel-test") { created(); }
    ~Context() override { destroy(); }
    const char *frontend() const override { return "test"; }
protected:
    void commitStringImpl(const std::string &) override {}
    void deleteSurroundingTextImpl(int, unsigned int) override {}
    void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
    void updatePreeditImpl() override {}
};
