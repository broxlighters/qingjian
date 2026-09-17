//! 一个 Fcitx 事件循环共享连接、能力协商与 owner 监视；不在按键上同步等待。
#pragma once
#include <fcitx-utils/dbus/bus.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/rect.h>
#include <functional>
#include <map>
#include <memory>
#include <string>
namespace qingjian::panel {
class GnomeBridge final {
public:
    static std::shared_ptr<GnomeBridge> shared(fcitx::EventLoop &loop);
    explicit GnomeBridge(fcitx::EventLoop &loop);
    ~GnomeBridge();
    fcitx::dbus::Message message(const char *member);
    bool ready() const { return !epoch_.empty(); }
    bool negotiating() const { return bool(pending_); }
    const std::string &epoch() const { return epoch_; }
    const std::string &reason() const { return reason_; }
    const std::vector<fcitx::Rect> &monitors() const { return monitors_; }
    uint64_t nextSubmission() { return ++submission_; }
    uint64_t subscribe(std::function<void(fcitx::dbus::Message *)> callback);
    void unsubscribe(uint64_t id) { listeners_.erase(id); }
    void hello();
private:
    void changed();
    void updateMonitors(const std::string &encoded);
    fcitx::EventLoop &loop_;

    fcitx::dbus::Bus bus_;

    std::unique_ptr<fcitx::dbus::Slot> owner_, signal_, pending_;

    std::unique_ptr<fcitx::EventSourceTime> retry_;

    std::map<uint64_t, std::function<void(fcitx::dbus::Message *)>> listeners_;

    std::string epoch_, reason_ = "extension_missing";

    uint64_t submission_ = 0, listener_ = 0, generation_ = 0;

    unsigned retries_ = 0;

    bool retryScheduled_ = false;

    std::vector<fcitx::Rect> monitors_;
};
}
