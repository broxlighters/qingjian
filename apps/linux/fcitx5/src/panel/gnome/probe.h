//! 仅阶段 0 使用的异步 Shell 实验桥接，显式启用并验证窗口按键来源。
#pragma once
#include "../size.h"
#include "identity/focus.h"
#include <fcitx-utils/dbus/bus.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/trackableobject.h>
#include <fcitx/inputcontext.h>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
struct QjRenderer;
struct QjResult;
namespace qingjian::panel {
class GnomeProbe final {
public:
    explicit GnomeProbe(fcitx::EventLoop &loop);
    ~GnomeProbe();
    bool render(fcitx::InputContext *context, std::shared_ptr<QjRenderer> renderer,
        nlohmann::json frame, SizeOptions size, double textScale, bool dark,
        std::function<bool()> valid, std::function<void(int)> action,
        std::function<void(const nlohmann::json &)> acknowledge, std::function<void()> fallback,
        const FocusIdentity &focusIdentity = {});
    void hide();
    static bool eligible(fcitx::InputContext *context);
private:
    void reset(bool revoke);
    void request(fcitx::dbus::Message message, std::function<void(fcitx::dbus::Message &)> done);
    void fail(const char *reason);
    void recovered();
    void geometryChanged();
    void call(const char *member, const std::function<void(fcitx::dbus::Message &)> &arguments,
        std::function<void(fcitx::dbus::Message &)> done);
    void upload(double raster, int width, int height);
    void event(fcitx::dbus::Message &message);
    fcitx::EventLoop &loop_;

    fcitx::dbus::Bus bus_;

    std::unique_ptr<fcitx::dbus::Slot> pending_;

    std::unique_ptr<fcitx::dbus::Slot> signal_;

    std::unique_ptr<fcitx::EventSourceTime> timer_;

    std::shared_ptr<QjRenderer> renderer_;

    std::shared_ptr<QjResult> result_;

    std::function<bool()> valid_;

    std::function<void(int)> action_;

    std::function<void(const nlohmann::json &)> acknowledge_;

    std::function<void()> fallback_;

    nlohmann::json frame_;

    SizeOptions size_;

    fcitx::TrackableObjectReference<fcitx::InputContext> context_;

    FocusIdentity focusIdentity_;

    double systemTextScale_ = 1;

    double textScale_ = 1;

    bool dark_ = false;

    bool painted_ = false;

    bool recovering_ = false;

    uint64_t serial_ = 0;

    uint64_t pendingDeadline_ = 0;

    /// 连续几何变化共用截止时间，成功绘制后才结束这次更新。
    uint64_t geometryDeadline_ = 0;

    std::string token_;
};
}
