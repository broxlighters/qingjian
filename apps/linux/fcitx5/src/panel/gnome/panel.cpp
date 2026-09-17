//! 主线程异步提交与合并最新帧；Hide 优先取消请求，晚到回执按完整身份丢弃。
#include "panel.h"
#include "reason.h"
#include "../status.h"
#include "qingjian_render.h"
#include <fcitx/inputpanel.h>
#include <fcitx-utils/log.h>
#include <cmath>
namespace qingjian::panel {
bool GnomePanel::eligible(fcitx::InputContext *context) {
    return context && context->frontendName() == "dbus" && context->display().starts_with("wayland:") &&
        context->capabilityFlags().test(fcitx::CapabilityFlag::RelativeRect);
}
GnomePanel::GnomePanel(fcitx::EventLoop &loop) : loop_(loop), bridge_(GnomeBridge::shared(loop)) {
    listener_ = bridge_->subscribe([this, weak = std::weak_ptr<int>(lifetime_)](auto *message) {
        if (!weak.expired()) event(message);
    });
}
GnomePanel::~GnomePanel() {
    lifetime_.reset();
    bridge_->unsubscribe(listener_);
    hide();
}
bool GnomePanel::valid() const {
    return current_ && current_->context.get() && current_->valid && current_->valid();
}
Submission GnomePanel::render(GnomeRequest request) {
    const auto &notice = request.frame.value("notice", nlohmann::json());
    if (request.frame.value("preedit", nlohmann::json::array()).empty() &&
        request.frame.at("candidates").at("items").empty() && (!notice.is_string() || notice.get<std::string>().empty())) {
        hide();
        recordStatus({{"backend", "gnome"}, {"state", "Idle"}, {"painted", false}});
        return Submission::Accepted;
    }
    if (!eligible(request.context.get()) || !request.valid || !request.valid() || !request.focus.key)
        return Submission::Failed;
    latest_ = std::make_unique<GnomeRequest>(std::move(request));
    if (!bridge_->ready()) {
        if (bridge_->negotiating()) {
            latest_->context.get()->inputPanel().setCustomInputPanelCallback([](fcitx::InputContext *) {});
            if (state_ != DisplayState::Preparing || !deadline_) {
                state_ = DisplayState::Preparing;
                deadline_ = fcitx::now(CLOCK_MONOTONIC) + 250000;
                timer_ = loop_.addTimeEvent(CLOCK_MONOTONIC, deadline_, 1000,
                    [this](auto *, auto) { fail("paint_timeout"); return false; });
            }
            return Submission::Pending;
        }
        failure_ = bridge_->reason();
        recordStatus({{"backend", "fcitx"}, {"state", "Fallback"}, {"reason", failure_}});
        state_ = DisplayState::Fallback;
        bridge_->hello();
        return Submission::Failed;
    }
    auto *context = latest_->context.get();
    context->inputPanel().setCustomInputPanelCallback([](fcitx::InputContext *) {});
    // 最多一条请求在途；后续输入只替换 latest，不排队重放旧帧。
    if (!pending_) begin();
    return Submission::Pending;
}
void GnomePanel::begin() {
    if (!latest_ || !bridge_->ready()) return;
    ++serial_;
    pending_.reset();
    current_ = std::move(latest_);
    preparing_.reset();
    if (!valid()) { hide(); return; }
    const auto &identity = current_->identity;
    token_ = nlohmann::json({{"generation", std::to_string(identity.generation)}, {"context", identity.context},
        {"revision", std::to_string(identity.revision)}, {"transport_epoch", bridge_->epoch()},
        {"focus_epoch", std::to_string(current_->focus.epoch)}, {"submission", std::to_string(bridge_->nextSubmission())},
        {"geometry_revision", "0"}}).dump();
    if (state_ == DisplayState::Visible || !deadline_) deadline_ = fcitx::now(CLOCK_MONOTONIC) + 250000;
    state_ = DisplayState::Preparing;
    recordStatus({{"backend", "gnome"}, {"state", "Preparing"}, {"painted", false}});
    // systemd 后端 accuracy=0 使用默认合并精度，可能把100ms续约拖到250ms。
    timer_ = loop_.addTimeEvent(CLOCK_MONOTONIC, deadline_, 1000, [this](auto *timer, uint64_t now) {
        if (state_ == DisplayState::Hiding) { fallback(); return false; }
        if (state_ != DisplayState::Visible) { fail("paint_timeout"); return true; }
        if (!valid()) { if (latest_) begin(); else hide(); return true; }
        if (!pending_) {
            auto renew = bridge_->message("Renew"); renew << token_;
            request(std::move(renew), [](auto &) {});
        }
        timer->setTime(now + 100000); timer->setEnabled(true);
        return true;
    });
    auto *context = current_->context.get();
    auto *object = dynamic_cast<fcitx::dbus::ObjectVTableBase *>(context);
    if (!object || !object->isRegistered() || !object->bus()) { fail("unsupported_frontend"); return; }
    auto daemon = object->bus()->createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetId");
    request(std::move(daemon), [this](auto &reply) {
        std::string daemon; reply >> daemon;
        if (!reply || daemon.empty()) { fail("focus_mismatch"); return; }
        bind(daemon);
    });
}
void GnomePanel::request(fcitx::dbus::Message message, std::function<void(fcitx::dbus::Message &)> done) {
    const auto serial = serial_;
    pending_ = message.callAsync(250000, [this, serial, done = std::move(done)](auto &reply) {
        if (serial != serial_) return true;
        auto slot = std::move(pending_);
        if (state_ == DisplayState::Hiding) return true;
        if (latest_) { begin(); return true; }
        if (!valid()) { hide(); return true; }
        if (reply.isError()) { fail(failureReason(reply.errorName())); return true; }
        try { done(reply); }
        catch (const std::exception &) { fail("protocol_mismatch"); }
        return true;
    });
}
void GnomePanel::bind(const std::string &daemon) {
    const auto &origin = *current_->focus.key;
    auto *context = current_->context.get();
    const auto rect = context->cursorRect();
    const auto scale = context->scaleFactor();
    if (rect == fcitx::Rect() || !std::isfinite(scale) || scale < 0.5 || scale > 4) {
        fail("anchor_unavailable"); return;
    }
    const auto source = nlohmann::json({{"bus", daemon}, {"sender", origin.sender}, {"path", origin.path},
        {"context", origin.context}, {"epoch", std::to_string(current_->focus.epoch)}, {"time", origin.time},
        {"code", origin.code}, {"frontend", "dbus"}, {"coordinates", "client-relative"},
        {"rect", {rect.left(), rect.top(), rect.width(), rect.height()}}, {"scale", scale}}).dump();
    auto call = bridge_->message("BindContext"); call << token_ << source;
    request(std::move(call), [this](auto &reply) {
        std::string encoded; reply >> encoded;
        auto geometry = nlohmann::json::parse(encoded, nullptr, false);
        if (!reply || !geometry.is_object()) { fail("anchor_unavailable"); return; }
        const auto raster = geometry.value("raster", 0.0);
        const auto width = geometry.value("width", 0), height = geometry.value("height", 0);
        const auto identity = geometry.value("identity", "");
        auto incoming = nlohmann::json::parse(identity, nullptr, false);
        auto expected = nlohmann::json::parse(token_);
        if (!incoming.is_object() || !incoming.contains("geometry_revision")) { fail("protocol_mismatch"); return; }
        expected["geometry_revision"] = incoming["geometry_revision"];
        if (incoming != expected || !std::isfinite(raster) || raster < 0.5 || raster > 4 ||
            width <= 0 || width > 1600 || height <= 0 || height > 900) { fail("scale_unresolved"); return; }
        token_ = identity;
        upload(raster, width, height);
    });
}
void GnomePanel::event(fcitx::dbus::Message *message) {
    if (!message) {
        if (!bridge_->ready()) { if (owns() && !bridge_->negotiating()) fail(bridge_->reason()); }
        else if (state_ == DisplayState::Preparing && latest_ && token_.empty()) begin();
        else if (state_ == DisplayState::Fallback && latest_ && latest_->valid && latest_->valid()) {
            // 通过 Engine 重新走默认面板清空/flush，不能直接让旧纹理覆盖回退面板。
            const auto recover = latest_->recover;
            if (recover) recover();
        }
        return;
    }
    std::string token; *message >> token;
    if (!*message || token != token_) return;
    const auto name = message->member();
    if (state_ == DisplayState::Hiding) { if (name == "Hidden") fallback(); return; }
    if (!valid()) { if (latest_ && !pending_) begin(); return; }
    if (name == "Painted") painted();
    else if (name == "Pointer") pointer(*message);
    else if (name == "Lost") {
        std::string reason; *message >> reason;
        fail(failureReason(reason, "focus_mismatch"));
    } else if (name == "Hidden") fail("focus_mismatch");
    else if (name == "GeometryChanged") {
        if (!latest_) latest_ = std::make_unique<GnomeRequest>(*current_);
        if (!pending_) begin();
    }
}
void GnomePanel::hide() {
    if (owns()) recordStatus({{"backend", "gnome"}, {"state", "Idle"}, {"painted", false}});
    ++serial_;
    pending_.reset(); timer_.reset();
    if (!token_.empty()) { auto hide = bridge_->message("Hide"); hide << token_; hide.send(); }
    token_.clear(); state_ = DisplayState::Idle; deadline_ = 0;
    current_.reset(); latest_.reset(); preparing_.reset(); visible_.reset();
}
void GnomePanel::fail(const std::string &reason) {
    if (state_ == DisplayState::Hiding) return;
    if (failure_ != reason) { failure_ = reason; FCITX_INFO() << "青简 GNOME 回退 reason=" << reason; }
    recordStatus({{"backend", "gnome"}, {"state", "Hiding"}, {"reason", reason}, {"painted", false}});
    state_ = DisplayState::Hiding;
    ++serial_;
    pending_.reset();
    // 未确认 Hidden 时先等待租约失效，避免晚到 Show 与默认面板重叠。
    timer_ = loop_.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 500000, 1000,
        [this](auto *, auto) { fallback(); return false; });
    if (token_.empty()) { fallback(); return; }
    auto call = bridge_->message("Hide"); call << token_;
    const auto serial = serial_;
    pending_ = call.callAsync(250000, [this, serial](auto &reply) {
        if (serial == serial_ && !reply.isError()) fallback();
        return true;
    });
}
void GnomePanel::fallback() {
    auto *request = latest_ ? latest_.get() : current_.get();
    auto callback = request && request->valid && request->valid() ? request->fallback : std::function<void()>();
    auto context = request ? request->context : fcitx::TrackableObjectReference<fcitx::InputContext>();
    auto recovery = request ? std::make_unique<GnomeRequest>(*request) : nullptr;
    hide();
    latest_ = std::move(recovery);
    state_ = DisplayState::Fallback;
    recordStatus({{"backend", "fcitx"}, {"state", "Fallback"}, {"painted", false}, {"reason", failure_}});
    if (auto *current = context.get()) current->inputPanel().setCustomInputPanelCallback({});
    if (callback) callback();
}
}
