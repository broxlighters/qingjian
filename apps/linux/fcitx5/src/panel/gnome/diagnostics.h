//! 阶段 0 上下文证据：只记录几何、能力和焦点，不记录输入内容。
#pragma once
#include "identity/key_origin.h"
#include <fcitx/inputcontext.h>
#include <fcitx-utils/log.h>
#include <cstdlib>
#include <nlohmann/json.hpp>

namespace qingjian::panel {
inline void traceKeyOrigin(const fcitx::KeyEvent &event) {
    if (!std::getenv("QINGJIAN_UI_DIAGNOSTICS") || !event.inputContext()) return;
    const auto caps = event.inputContext()->capabilityFlags();
    if (caps == fcitx::CapabilityFlags() || caps.testAny(fcitx::CapabilityFlag::PasswordOrSensitive) ||
        caps.test(fcitx::CapabilityFlag::Disable)) return;
    const auto origin = KeyOrigin::capture(event);
    // 时间戳和键码只留在内存；诊断不得记录用户按键或候选文本。
    FCITX_INFO() << "青简来源诊断：" << nlohmann::json({
        {"verified_dispatch", origin.has_value()},
        {"sender", origin ? origin->sender : ""}, {"path", origin ? origin->path : ""},
        {"context", origin ? origin->context : ""}, {"method", origin ? origin->method : ""},
        {"window_verified", false}}).dump();
}
inline void traceContext(const fcitx::InputContext *context, const char *event) {
    if (!std::getenv("QINGJIAN_UI_DIAGNOSTICS") || !context) return;
    const auto caps = context->capabilityFlags();
    if (caps == fcitx::CapabilityFlags() || caps.testAny(fcitx::CapabilityFlag::PasswordOrSensitive) ||
        caps.test(fcitx::CapabilityFlag::Disable)) return;
    const auto &rect = context->cursorRect();
    FCITX_INFO() << "青简上下文诊断：" << nlohmann::json({
        {"event", event}, {"frontend", context->frontend()}, {"program", context->program()},
        {"display", context->display()}, {"focus", context->hasFocus()},
        {"relative_rect", caps.test(fcitx::CapabilityFlag::RelativeRect)},
        {"context_scale", context->scaleFactor()},
        {"cursor", {rect.left(), rect.top(), rect.width(), rect.height()}}}).dump();
}
}
