//! 实验通路以同源按键及 IC 代次关联 Shell 窗口；无法证明时保留框架 UI。
#include "probe.h"
#include "../interaction/action.h"
#include "qingjian_render.h"
#include <fcitx/inputpanel.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/unixfd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <cmath>
namespace qingjian::panel {
namespace {
constexpr const char *service = "org.qingjian.PanelProbe1";
constexpr const char *path = "/org/qingjian/PanelProbe1";
}
bool GnomeProbe::eligible(fcitx::InputContext *context) {
    const auto *enabled = std::getenv("QINGJIAN_GNOME_PROBE");
    return enabled && std::string(enabled) == "1" && context &&
        context->frontendName() == "dbus" && context->display().starts_with("wayland:") &&
        context->capabilityFlags().test(fcitx::CapabilityFlag::RelativeRect);
}
GnomeProbe::GnomeProbe(fcitx::EventLoop &loop) : loop_(loop), bus_(fcitx::dbus::BusType::Session) {
    if (!bus_.isOpen()) return;
    bus_.attachEventLoop(&loop_);
    signal_ = bus_.addMatch(fcitx::dbus::MatchRule(service, path, service), [this](fcitx::dbus::Message &message) {
        event(message);
        return true;
    });
}
GnomeProbe::~GnomeProbe() { hide(); }
void GnomeProbe::call(const char *member, const std::function<void(fcitx::dbus::Message &)> &arguments,
    std::function<void(fcitx::dbus::Message &)> done) {
    auto message = bus_.createMethodCall(service, path, service, member);
    arguments(message);
    request(std::move(message), std::move(done));
}
void GnomeProbe::request(fcitx::dbus::Message message, std::function<void(fcitx::dbus::Message &)> done) {
    const auto serial = serial_;
    const auto method = message.member();
    pendingDeadline_ = fcitx::now(CLOCK_MONOTONIC) + 250000;
    pending_ = message.callAsync(250000, [this, serial, method, done = std::move(done)](fcitx::dbus::Message &reply) {
        if (serial != serial_) return true;
        auto current = std::move(pending_);
        if (!valid_ || !valid_()) {
            if (std::getenv("QINGJIAN_UI_DIAGNOSTICS")) FCITX_INFO() << "青简 GNOME 旧回调失效 method=" << method;
            hide(); return true;
        }
        if (reply.isError()) {
            if (std::getenv("QINGJIAN_UI_DIAGNOSTICS"))
                FCITX_INFO() << "青简 GNOME 请求失败 method=" << method << " error=" << reply.errorName();
            fail("transport_lost"); return true;
        }
        done(reply);
        return true;
    });
}
bool GnomeProbe::render(fcitx::InputContext *context, std::shared_ptr<QjRenderer> renderer,
    nlohmann::json frame, SizeOptions size, double textScale, bool dark,
    std::function<bool()> valid, std::function<void(int)> action,
    std::function<void(const nlohmann::json &)> acknowledge, std::function<void()> fallback,
    const FocusIdentity &focusIdentity) {
    reset(false);
    if (!eligible(context) || !bus_.isOpen() || !valid || !valid() || !focusIdentity.key) {
        hide(); return false;
    }
    if (frame.value("preedit", nlohmann::json::array()).empty() &&
        frame.at("candidates").at("items").empty() && frame.value("notice", nlohmann::json()).is_null()) return false;
    renderer_ = std::move(renderer); frame_ = std::move(frame); size_ = size;
    context_ = context->watch(); focusIdentity_ = focusIdentity;
    systemTextScale_ = textScale; textScale_ = size.textScale(textScale); dark_ = dark;
    geometryDeadline_ = fcitx::now(CLOCK_MONOTONIC) + 250000;
    valid_ = std::move(valid); action_ = std::move(action);
    acknowledge_ = std::move(acknowledge); fallback_ = std::move(fallback);
    token_ = std::to_string(serial_);
    const auto rect = context->cursorRect();
    const auto contextScale = context->scaleFactor();
    if (!std::isfinite(contextScale) || contextScale < 0.5 || contextScale > 4 || rect == fcitx::Rect()) {
        hide(); return false;
    }
    // 此处安装空回调只用于有截止时间的原型准备，失败后恢复框架 UI。
    context->inputPanel().setCustomInputPanelCallback([](fcitx::InputContext *) {});
    timer_ = loop_.addTimeEvent(CLOCK_MONOTONIC, geometryDeadline_, 1,
        [this](fcitx::EventSourceTime *timer, uint64_t now) {
            if (recovering_) { recovered(); return false; }
            if (!painted_) { fail("paint_timeout"); return true; }
            if (!valid_ || !valid_()) { fail("focus_mismatch"); return true; }
            if (pending_ && now >= pendingDeadline_) { fail("transport_lost"); return true; }
            // 一个续约在途；保留其250ms截止时间，不能被100ms tick反复取消。
            if (!pending_) call("Renew", [this](auto &m) { m << token_; }, [](auto &) {});
            timer->setTime(now + 100000);
            timer->setEnabled(true);
            return true;
        });
    // 在实际 frontend 所在连接查询 daemon ID；portal 可复用唯一名，不能拿字符串假设同总线。
    auto *object = dynamic_cast<fcitx::dbus::ObjectVTableBase *>(context);
    if (!object || !object->isRegistered() || !object->bus()) { hide(); return false; }
    auto daemon = object->bus()->createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "GetId");
    request(std::move(daemon), [this, focusIdentity, rect, contextScale](auto &daemonReply) {
        std::string daemonId; daemonReply >> daemonId;
        if (!daemonReply || daemonId.empty()) { fail("source_unresolved"); return; }
        call("Hello", [](auto &m) { m << uint32_t(2); }, [this, daemonId, focusIdentity, rect, contextScale](auto &reply) {
            uint32_t version = 0; reply >> version;
            if (!reply || version != 2) { fail("protocol_mismatch"); return; }
            call("Bind", [this, &daemonId, &focusIdentity, &rect, contextScale](auto &m) {
                const auto &origin = *focusIdentity.key;
                m << token_ << daemonId << origin.sender << origin.path << origin.context << std::to_string(focusIdentity.epoch)
                  << origin.time << origin.code << int32_t(rect.left()) << int32_t(rect.top())
                  << int32_t(rect.width()) << int32_t(rect.height()) << contextScale;
            }, [this](auto &geometry) {
                double raster = 0; uint32_t width = 0, height = 0;
                geometry >> raster >> width >> height;
                if (!geometry || !std::isfinite(raster) || raster < 0.5 || raster > 4 || !width || !height) {
                    fail("scale_unresolved"); return;
                }
                upload(raster, std::min(width, 1600U), std::min(height, 900U));
            });
        });
    });
    return true;
}
void GnomeProbe::upload(double raster, int width, int height) {
    const auto body = frame_.dump();
    result_.reset(qj_renderer_render_sized(renderer_.get(), QJ_RENDER_ABI_VERSION, QJ_RENDER_SIZE_ABI_VERSION,
        reinterpret_cast<const uint8_t *>(body.data()), body.size(), raster, size_.uiScale,
        textScale_, width, height, dark_), qj_result_destroy);
    QjImageInfo image{};
    if (!result_ || !qj_result_image(result_.get(), &image) || image.width == 1 || image.height == 1) {
        fail("renderer_unavailable"); return;
    }
    auto fd = fcitx::UnixFD::own(memfd_create("qingjian-probe-frame", MFD_CLOEXEC | MFD_ALLOW_SEALING));
    if (fd.fd() < 0) { fail("buffer_invalid"); return; }
    size_t offset = 0;
    while (offset < image.length) {
        const auto count = write(fd.fd(), image.pixels + offset, image.length - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { fail("buffer_invalid"); return; }
        offset += count;
    }
    if (lseek(fd.fd(), 0, SEEK_SET) != 0 ||
        fcntl(fd.fd(), F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL) < 0) {
        fail("buffer_invalid"); return;
    }
    call("Prepare", [this, &fd, &image](auto &m) {
        m << token_ << fd << image.width << image.height << image.stride << uint32_t(image.length);
    }, [this](auto &) {
        call("Show", [this](auto &m) { m << token_; }, [](auto &) {});
    });
}
void GnomeProbe::event(fcitx::dbus::Message &message) {
    std::string token; message >> token;
    if (!message || token != token_ || !valid_ || !valid_()) return;
    if (recovering_) {
        if (message.member() == "Hidden") recovered();
        return;
    }
    if (message.member() == "GeometryChanged") {
        geometryChanged();
        return;
    }
    if (message.member() == "Painted") {
        if (painted_) return;
        painted_ = true;
        geometryDeadline_ = 0;
        if (timer_) { timer_->setTime(fcitx::now(CLOCK_MONOTONIC) + 100000); timer_->setEnabled(true); }
        auto senses = nlohmann::json::array();
        uint32_t row = 0, sense = 0;
        for (uint32_t i = 0; i < 128 && qj_result_exposure(result_.get(), i, &row, &sense); ++i)
            senses.push_back({row, sense});
        FCITX_INFO() << "青简 GNOME 原型 painted submission=" << token_;
        auto ack = acknowledge_;
        if (ack) ack(senses);
    } else if (message.member() == "Pointer" && painted_) {
        uint32_t x = 0, y = 0, button = 0; message >> x >> y >> button;
        if (!message) return;
        const auto row = qj_result_hit(result_.get(), x, y);
        if (button == 1 && row < 0) {
            const auto page = qj_result_page(result_.get(), x, y);
            if (page) button = page < 0 ? 4 : 5;
        }
        if (auto mapped = action(button, row, frame_.value("page", 0U) > 0,
            frame_.value("page", 0U) + 1 < frame_.value("page_count", 1U))) {
            auto callback = action_;
            painted_ = false;
            if (callback) callback(*mapped);
        }
    } else if (message.member() == "Hidden") {
        fail("focus_mismatch");
    }
}
void GnomeProbe::geometryChanged() {
    const auto now = fcitx::now(CLOCK_MONOTONIC);
    const auto deadline = geometryDeadline_ ? geometryDeadline_ : now + 250000;
    auto *context = context_.get();
    if (!context || now >= deadline) { fail("geometry_timeout"); return; }
    // render会撤销旧回调；先完整复制参数，且保留原更新的截止时间。
    auto renderer = renderer_;
    auto frame = frame_;
    auto valid = valid_;
    auto action = action_;
    auto acknowledge = acknowledge_;
    auto fallback = fallback_;
    const auto focus = focusIdentity_;
    const auto size = size_;
    const auto textScale = systemTextScale_;
    const auto dark = dark_;
    if (!render(context, renderer, frame, size, textScale, dark, valid, action, acknowledge, fallback, focus)) {
        if (fallback) fallback();
        return;
    }
    geometryDeadline_ = deadline;
    timer_->setTime(deadline);
    timer_->setEnabled(true);
}
void GnomeProbe::hide() {
    reset(true);
}
void GnomeProbe::reset(bool revoke) {
    ++serial_; painted_ = false; recovering_ = false; token_.clear();
    pending_.reset(); timer_.reset();
    if (bus_.isOpen()) {
        auto message = bus_.createMethodCall(service, path, service, revoke ? "Hide" : "Withdraw");
        message.send();
    }
    result_.reset(); renderer_.reset(); frame_ = nullptr;
    context_.unwatch(); focusIdentity_ = {}; geometryDeadline_ = 0;
    valid_ = {}; action_ = {}; acknowledge_ = {}; fallback_ = {};
}
void GnomeProbe::fail(const char *reason) {
    if (recovering_) return;
    FCITX_INFO() << "青简 GNOME 原型回退：" << reason;
    painted_ = false;
    recovering_ = true;
    pending_.reset();
    // 先等 Hidden/Hide 应答；对端丢失时等500ms显示租约，避免恢复默认面板后晚到Show。
    if (timer_) { timer_->setTime(fcitx::now(CLOCK_MONOTONIC) + 500000); timer_->setEnabled(true); }
    auto message = bus_.createMethodCall(service, path, service, "Hide");
    const auto serial = serial_;
    pending_ = message.callAsync(250000, [this, serial](fcitx::dbus::Message &reply) {
        if (serial != serial_) return true;
        auto current = std::move(pending_);
        if (!reply.isError()) recovered();
        return true;
    });
}
void GnomeProbe::recovered() {
    auto fallback = fallback_;
    hide();
    if (fallback) fallback();
}
}
