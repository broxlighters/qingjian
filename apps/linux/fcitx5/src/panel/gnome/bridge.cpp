//! 连接代次由扩展签发；owner 改变清能力，退避不阻塞输入线程。
#include "bridge.h"
#include "reason.h"
#include <nlohmann/json.hpp>
#include <algorithm>
namespace qingjian::panel {
namespace {
constexpr const char *service = "org.qingjian.Panel1";
constexpr const char *path = "/org/qingjian/Panel1";
}
std::shared_ptr<GnomeBridge> GnomeBridge::shared(fcitx::EventLoop &loop) {
    static std::map<fcitx::EventLoop *, std::weak_ptr<GnomeBridge>> bridges;
    auto bridge = bridges[&loop].lock();
    if (!bridge) { bridge = std::make_shared<GnomeBridge>(loop); bridges[&loop] = bridge; }
    return bridge;
}
GnomeBridge::GnomeBridge(fcitx::EventLoop &loop) : loop_(loop), bus_(fcitx::dbus::BusType::Session) {
    if (!bus_.isOpen()) return;
    bus_.attachEventLoop(&loop_);
    owner_ = bus_.addMatch(fcitx::dbus::MatchRule("org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "NameOwnerChanged", {service}), [this](auto &) { changed(); return true; });
    signal_ = bus_.addMatch(fcitx::dbus::MatchRule(service, path, service), [this](auto &message) {
        if (message.member() == "OutputsChanged") {
            std::string encoded; message >> encoded;
            updateMonitors(encoded);
            return true;
        }
        // 回调可能销毁订阅者；快照并再次检查登记，不访问已擦除的闭包。
        const auto callbacks = listeners_;
        for (const auto &[id, callback] : callbacks) if (listeners_.contains(id)) {
            message.rewind();
            callback(&message);
        }
        return true;
    });
    hello();
}
GnomeBridge::~GnomeBridge() = default;
fcitx::dbus::Message GnomeBridge::message(const char *member) {
    return bus_.createMethodCall(service, path, service, member);
}
uint64_t GnomeBridge::subscribe(std::function<void(fcitx::dbus::Message *)> callback) {
    const auto id = ++listener_;
    listeners_[id] = std::move(callback);
    return id;
}
void GnomeBridge::changed() {
    ++generation_;
    pending_.reset();
    retry_.reset();
    retryScheduled_ = false;
    epoch_.clear();
    monitors_.clear();
    reason_ = "transport_lost";
    const auto callbacks = listeners_;
    for (const auto &[id, callback] : callbacks) if (listeners_.contains(id)) callback(nullptr);
    retries_ = 0;
    hello();
}
void GnomeBridge::hello() {
    if (!bus_.isOpen() || pending_ || retryScheduled_ || ready()) return;
    auto request = message("Hello");
    request << uint32_t(1);
    const auto generation = generation_;
    pending_ = request.callAsync(250000, [this, generation](auto &reply) {
        if (generation != generation_) return true;
        auto slot = std::move(pending_);
        std::string encoded;
        if (!reply.isError()) reply >> encoded;
        const auto caps = nlohmann::json::parse(encoded, nullptr, false);
        if (reply && caps.is_object() && caps.contains("version") && caps["version"] == 1 &&
            caps.contains("format") && caps["format"] == "RGBA_8888_PRE" &&
            caps.contains("length") && caps["length"] == 5760000 &&
            caps.contains("transport_epoch") && caps["transport_epoch"].is_string()) {
            epoch_ = caps["transport_epoch"].get<std::string>();
            updateMonitors(caps.value("monitors", nlohmann::json()).dump());
            reason_.clear();
            retries_ = 0;
            const auto callbacks = listeners_;
            for (const auto &[id, callback] : callbacks) if (listeners_.contains(id)) callback(nullptr);
        } else {
            reason_ = reply.isError() ? failureReason(reply.errorName(), "extension_missing") : "protocol_mismatch";
            constexpr uint64_t delays[] = {250000, 500000, 1000000, 2000000, 5000000};
            const auto delay = delays[std::min(retries_++, 4U)];
            retryScheduled_ = true;
            retry_ = loop_.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + delay, 0,
                [this](auto *, auto) { retryScheduled_ = false; hello(); return false; });
            const auto callbacks = listeners_;
            for (const auto &[id, callback] : callbacks) if (listeners_.contains(id)) callback(nullptr);
        }
        return true;
    });
}
void GnomeBridge::updateMonitors(const std::string &encoded) {
    monitors_.clear();
    if (encoded.size() > 8192) return;
    const auto monitors = nlohmann::json::parse(encoded, nullptr, false);
    if (!monitors.is_array() || monitors.size() > 64) return;
    std::vector<fcitx::Rect> next;
    for (const auto &area : monitors) {
        if (!area.is_array() || area.size() != 4 || !std::all_of(area.begin(), area.end(),
            [](const auto &value) { return value.is_number_integer() && value >= -65536 && value <= 65536; })) return;
        const int x = area[0], y = area[1], width = area[2], height = area[3];
        if (width <= 0 || width > 32768 || height <= 0 || height > 32768) return;
        next.emplace_back(x, y, x + width, y + height);
    }
    monitors_ = std::move(next);
}
}
