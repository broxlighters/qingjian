//! IC 焦点代次跟随框架事件；Qt 的多个输入框可复用同一 InputContext。
#pragma once
#include "key_origin.h"
namespace qingjian::panel {
struct FocusIdentity final {
    uint64_t epoch = 1;

    std::optional<KeyOrigin> key;

    void invalidate() { ++epoch; key.reset(); }

    void observe(const fcitx::KeyEvent &event) {
        if (!event.isRelease()) key = KeyOrigin::capture(event);
    }
};
}
