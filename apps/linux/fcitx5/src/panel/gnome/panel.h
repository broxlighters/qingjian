//! GNOME 连续展示状态机；异步请求从不保存裸 InputContext 或裸位图结果。
#pragma once
#include "bridge.h"
#include "request.h"
#include "../backend/submission.h"
#include "../lifecycle/state.h"
struct QjResult;
namespace qingjian::panel {
class GnomePanel final {
public:
    explicit GnomePanel(fcitx::EventLoop &loop);
    ~GnomePanel();
    Submission render(GnomeRequest request);
    void hide();
    bool owns() const { return state_ == DisplayState::Preparing || state_ == DisplayState::AwaitingPaint ||
        state_ == DisplayState::Visible || state_ == DisplayState::Hiding; }
    DisplayState state() const { return state_; }
    static bool eligible(fcitx::InputContext *context);
private:
    void begin();
    void bind(const std::string &daemon);
    void upload(double raster, int width, int height);
    void request(fcitx::dbus::Message message, std::function<void(fcitx::dbus::Message &)> done);
    void event(fcitx::dbus::Message *message);
    void fail(const std::string &reason);
    void fallback();
    void painted();
    void pointer(fcitx::dbus::Message &message);
    bool valid() const;
    fcitx::EventLoop &loop_;

    std::shared_ptr<GnomeBridge> bridge_;

    uint64_t listener_ = 0, serial_ = 0, deadline_ = 0;

    std::unique_ptr<fcitx::dbus::Slot> pending_;

    std::unique_ptr<fcitx::EventSourceTime> timer_;

    std::unique_ptr<GnomeRequest> current_, latest_;

    std::shared_ptr<QjResult> preparing_, visible_;

    std::string token_, failure_;

    DisplayState state_ = DisplayState::Idle;

    /// 订阅快照回调可能晚于 unsubscribe；弱标记在析构第一步失效。
    std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
};
}
