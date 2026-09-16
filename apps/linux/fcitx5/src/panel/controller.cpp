//! 展示失败只切回默认 UI，不重发按键或 Commit。
#include "controller.h"
#include "backend/probe.h"
#include "backend/selector.h"
#include "interaction/action.h"
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx-utils/log.h>
#include <chrono>
#include <cmath>
#include <future>
#include <cstdlib>
#include <exception>
#if defined(QJ_RENDER_FFI)
#include "qingjian_render.h"
#endif
namespace qingjian::panel {
namespace {
#if defined(QJ_RENDER_FFI)
// 字体扫描在插件构造时启动；首帧未完成时直接保留 Fcitx。
std::shared_ptr<QjRenderer> renderer() {
    static auto pending = std::async(std::launch::async, [] {
        return std::shared_ptr<QjRenderer>(qj_renderer_create(QJ_RENDER_ABI_VERSION), qj_renderer_destroy);
    });
    static std::shared_ptr<QjRenderer> ready;
    if (!ready && pending.valid() && pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        ready = pending.get();
    return ready;
}
#endif
}
void prepareRenderer() try {
#if defined(QJ_RENDER_FFI)
    (void)renderer();
#endif
} catch (const std::exception &) {
    FCITX_WARN() << "青简字体初始化未启动，保留 Fcitx 面板";
}
bool x11Display(const std::string &display) {
    return displayKind(display) == DisplayKind::X11;
}
Controller::Controller(std::unique_ptr<Backend> backend)
    : backend_(std::move(backend)), injected_(bool(backend_)) {}
Controller::~Controller() {
    health_.reset();
    io_.reset();
    if (backend_) backend_->hide();
    releaseResult();
    if (clearTextCache_) clearTextCache_();
}
void Controller::configure(RendererMode mode, fcitx::EventLoop *loop) {
    mode_ = mode;
    loop_ = loop;
}
bool Controller::eligible(fcitx::InputContext *context) const {
    return rejection(context) == nullptr;
}
const char *Controller::rejection(fcitx::InputContext *context) const {
    if (mode_ == RendererMode::Fcitx) return "配置使用 Fcitx 面板";
    if (!context || !context->hasFocus()) return "输入上下文没有焦点";
    if (context->capabilityFlags().testAny(fcitx::CapabilityFlag::PasswordOrSensitive) ||
        context->capabilityFlags().test(fcitx::CapabilityFlag::Disable) ||
        context->capabilityFlags() == fcitx::CapabilityFlags()) return "私密或未知输入能力";
#if defined(QJ_RENDER_FFI)
    const auto probe = probeBackend(context->display());
    if (!probe.available && !(injected_ && probe.display == DisplayKind::X11)) return probe.reason;
    if (mode_ == RendererMode::Auto && !probe.validated) return "该后端尚未通过桌面和性能验收，auto 保留默认面板";
    if (!loop_) return "主事件循环不可用";
    if (probe.display == DisplayKind::X11 && context->cursorRect() == fcitx::Rect()) return "X11 光标坐标不可用";
    return nullptr;
#else
    return "当前构建未启用自绘";
#endif
}
void Controller::releaseResult() {
#if defined(QJ_RENDER_FFI)
    qj_result_destroy(static_cast<QjResult *>(result_));
#endif
    result_ = nullptr;
}
bool Controller::renderFrame(fcitx::InputContext *context, const nlohmann::json &frame,
                             FrameIdentity identity, std::function<bool()> valid,
                             std::function<void(int)> actionCallback,
                             std::function<void(const nlohmann::json &)> acknowledge,
                             std::function<void()> fallback, std::chrono::steady_clock::time_point ready,
                             bool systemDark) try {
    hide(context);
    ready_ = ready;
#if defined(QJ_RENDER_FFI)
    if (const auto *reason = rejection(context)) return unavailable(reason);
    if (!valid || !valid()) return unavailable("帧身份已失效");
    auto engine = renderer();
    if (!engine) return unavailable("字体尚未就绪");
    clearTextCache_ = [weak = std::weak_ptr<QjRenderer>(engine)] {
        if (auto current = weak.lock()) qj_renderer_clear_text_cache(current.get());
    };
    if ((!display_.empty() && display_ != context->display()) || (backendFailed_ && !injected_)) {
        health_.reset();
        io_.reset();
        backend_.reset();
    }
    backendFailed_ = false;
    display_ = context->display();
    const char *backendReason = "窗口承载不可用";
    if (!backend_) backend_ = openBackend(display_, &backendReason);
    if (!backend_) return unavailable(backendReason);
    const auto scale = context->scaleFactor();
    if (!std::isfinite(scale) || scale < 0.5 || scale > 4.0) return unavailable("缩放超出范围");
    const auto bounds = backend_->bounds(context->cursorRect());
    if (bounds.width() <= 0 || bounds.height() <= 0) return unavailable("没有有效的屏幕可用区域");
    const auto body = frame.dump();
    result_ = qj_renderer_render(engine.get(), QJ_RENDER_ABI_VERSION,
        reinterpret_cast<const uint8_t *>(body.data()), body.size(), scale,
        std::min(bounds.width(), 1600), std::min(bounds.height(), 900),
        systemDark);
    if (!result_) return unavailable("位图渲染失败");
    QjImageInfo image{};
    if (!qj_result_image(static_cast<QjResult *>(result_), &image) ||
        (image.width == 1 && image.height == 1)) { releaseResult(); return false; }
    identity_ = std::move(identity);
    valid_ = std::move(valid);
    action_ = std::move(actionCallback);
    acknowledge_ = std::move(acknowledge);
    fallback_ = std::move(fallback);
    previous_ = frame.value("page", 0U) > 0;
    next_ = frame.value("page", 0U) + 1 < frame.value("page_count", 1U);
    const auto submission = submission_;
    io_ = loop_->addIOEvent(backend_->fd(), fcitx::IOEventFlags(fcitx::IOEventFlag::In) | fcitx::IOEventFlag::Err | fcitx::IOEventFlag::Hup,
        [this, context, submission](fcitx::EventSourceIO *, int, fcitx::IOEventFlags flags) {
            return pollEvents(context, submission, flags);
        });
    if (!io_) { hide(context); return unavailable("无法注册窗口事件"); }
    health_ = loop_->addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 250000, 0,
        [this, context, submission](fcitx::EventSourceTime *timer, uint64_t now) {
            return checkHealth(context, submission, timer, now);
        });
    if (!health_) { hide(context); return unavailable("无法注册窗口健康检查"); }
    registerCallback(context);
    return true;
#else
    (void)frame; (void)identity; (void)valid; (void)actionCallback;
    (void)acknowledge; (void)fallback; (void)systemDark;
    return unavailable("当前构建未启用自绘");
#endif
} catch (const std::exception &) {
    hide(context);
    return unavailable("渲染初始化异常");
}
// 事件源可在点击回调中同步销毁。先把闭包参数复制进普通调用栈，
// 此后不再读取旧闭包；承载由局部 shared_ptr 持有到 poll 返回。
bool Controller::pollEvents(fcitx::InputContext *context, uint64_t submission, fcitx::IOEventFlags flags) try {
    auto backend = backend_;
    if (submission != submission_) return true;
    const bool failed = flags.test(fcitx::IOEventFlag::Err) || flags.test(fcitx::IOEventFlag::Hup) ||
        !backend->poll([this, submission](int x, int y, unsigned b) {
            return submission == submission_ && click(x, y, b);
        });
    if (failed && submission == submission_) fail(context);
    return true;
} catch (const std::exception &) {
    if (submission == submission_) fail(context);
    return true;
}
bool Controller::checkHealth(fcitx::InputContext *context, uint64_t submission, fcitx::EventSourceTime *timer, uint64_t now) try {
    auto backend = backend_;
    if (submission != submission_) return true;
    if (!backend || !backend->healthy()) { fail(context); return true; }
    // 同步查询可能把 X 事件读进内部队列，fd 未必再次可读。
    const bool ok = backend->poll([this, submission](int x, int y, unsigned b) {
        return submission == submission_ && click(x, y, b);
    });
    if (submission != submission_) return true;
    if (!ok) { fail(context); return true; }
    timer->setTime(now + 250000);
    timer->setEnabled(true);
    return true;
} catch (const std::exception &) {
    if (submission == submission_) fail(context);
    return true;
}
bool Controller::unavailable(const char *reason) {
    if (mode_ != RendererMode::Fcitx && failure_ != reason) {
        failure_ = reason;
        FCITX_INFO() << "青简保留 Fcitx 面板：" << reason;
    }
    return false;
}
void Controller::registerCallback(fcitx::InputContext *context) {
    if (context && result_) context->inputPanel().setCustomInputPanelCallback(
        [this](fcitx::InputContext *ic) { refresh(ic); });
}
void Controller::fail(fcitx::InputContext *context) {
    backendFailed_ = true;
    unavailable("窗口提交或显示条件发生变化");
    auto fallback = fallback_;
    hide(context);
    if (fallback) fallback();
}
void Controller::refresh(fcitx::InputContext *context) try {
#if defined(QJ_RENDER_FFI)
    if (!result_) return;
    if (!valid_ || !valid_()) { hide(context); return; }
    if (!eligible(context) || display_ != context->display()) { fail(context); return; }
    QjImageInfo image{};
    auto *result = static_cast<QjResult *>(result_);
    const auto upload = std::chrono::steady_clock::now();
    if (!qj_result_image(result, &image) || !backend_->present(image.pixels,
            image.width, image.height, image.stride, context->cursorRect())) {
        fail(context);
        return;
    }
    active_ = true;
    failure_.clear();
    if (std::getenv("QINGJIAN_UI_TIMINGS")) {
        const auto done = std::chrono::steady_clock::now();
        const auto timing = backend_->timing();
        FCITX_INFO() << "青简 UI 耗时 ns: total=" << std::chrono::duration_cast<std::chrono::nanoseconds>(done - ready_).count()
                     << " layout=" << image.layout_ns << " raster=" << image.raster_ns
                     << " backend=" << std::chrono::duration_cast<std::chrono::nanoseconds>(done - upload).count()
                     << " probe=" << timing.probeNs << " convert=" << timing.convertNs << " upload=" << timing.uploadNs
                     << " commit=" << timing.commitNs << " upload_bytes=" << timing.uploadBytes;
    }
    auto senses = nlohmann::json::array();
    uint32_t row = 0, sense = 0;
    // 协议最多接收 128 个义项；超长列表保守少记，不能断开输入连接。
    for (uint32_t i = 0; i < 128 && qj_result_exposure(result, i, &row, &sense); ++i)
        senses.push_back({row, sense});
    auto ack = acknowledge_;
    if (ack) ack(senses);
#else
    (void)context;
#endif
} catch (const std::exception &) {
    fail(context);
}
bool Controller::click(int x, int y, unsigned button) {
#if defined(QJ_RENDER_FFI)
    if (!active_ || !valid_ || !valid_()) return false;
    if (x < 0 || y < 0) return true;
    const auto row = qj_result_hit(static_cast<QjResult *>(result_), x, y);
    if (button == 1 && row < 0) {
        auto page = qj_result_page(static_cast<QjResult *>(result_), x, y);
        if (page) button = page < 0 ? 4 : 5;
    }
    if (auto mapped = action(button, row, previous_, next_)) {
        auto callback = action_;
        if (callback) callback(*mapped);
        return false;
    }
#else
    (void)x; (void)y; (void)button;
#endif
    return true;
}
void Controller::hide(fcitx::InputContext *context) {
    ++submission_;
    active_ = false;
    identity_ = {};
    if (health_) health_->setEnabled(false);
    if (io_) io_->setEnabled(false);
    if (backend_) { backend_->hide(); backend_->drain(); }
    releaseResult();
    valid_ = {}; action_ = {}; acknowledge_ = {}; fallback_ = {};
    if (context) context->inputPanel().setCustomInputPanelCallback({});
}
void Controller::invalidate(fcitx::InputContext *context) {
    hide(context);
    if (!injected_) {
        health_.reset();
        io_.reset();
        backend_.reset();
        display_.clear();
    }
    if (clearTextCache_) clearTextCache_();
}
bool Controller::accepts(const FrameIdentity &identity) const {
    return active_ && identity_ == identity && valid_ && valid_();
}
}
