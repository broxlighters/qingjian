//! 一个有效候选快照；最多保存当前提交和待提交最新帧，回调验证上下文弱引用。
#pragma once
#include "identity/focus.h"
#include "../identity.h"
#include "../size.h"
#include <fcitx-utils/trackableobject.h>
#include <fcitx/inputcontext.h>
#include <nlohmann/json.hpp>
#include <functional>
#include <memory>
struct QjRenderer;
namespace qingjian::panel {
struct GnomeRequest {
    fcitx::TrackableObjectReference<fcitx::InputContext> context;

    std::shared_ptr<QjRenderer> renderer;

    nlohmann::json frame;

    FrameIdentity identity;

    FocusIdentity focus;

    SizeOptions size;

    double textScale = 1;

    bool dark = false;

    std::function<bool()> valid;

    std::function<void(int)> action;

    std::function<void(const nlohmann::json &)> acknowledge;

    std::function<void()> fallback;

    std::function<void()> recover;

    bool textScaleKnown = false;
};
}
