//! 来源测试的键盘布局占位符；真实 KeyEvent 由测试观察器消费。
#pragma once
#include <fcitx/inputmethodengine.h>
class KeyboardEngine final : public fcitx::InputMethodEngine {
public:
    void keyEvent(const fcitx::InputMethodEntry &, fcitx::KeyEvent &) override {}
};
