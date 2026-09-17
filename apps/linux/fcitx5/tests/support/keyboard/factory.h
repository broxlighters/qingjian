//! 来源测试的静态 keyboard 依赖；dbus/frontend 仍加载系统原库。
#pragma once
#include "engine.h"
#include <fcitx/addonfactory.h>
class KeyboardFactory final : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *) override { return new KeyboardEngine; }
};
