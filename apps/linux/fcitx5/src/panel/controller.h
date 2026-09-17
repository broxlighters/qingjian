//! 青简上下文的面板接管、帧验证、窗口与 Rust 结果生命周期。
#pragma once
#include "identity.h"
#include "mode.h"
#include "size.h"
#include "gnome/identity/focus.h"
#include "backend/base.h"
#include "backend/submission.h"
#include <fcitx-utils/event.h>
#include <nlohmann/json.hpp>
#include <functional>
#include <chrono>
#include <memory>
#include <string>
namespace fcitx { class InputContext; }
namespace qingjian::panel {
#if defined(QJ_GNOME_BACKEND)
class GnomePanel;
#endif
#if defined(QJ_GNOME_PROBE)
class GnomeProbe;
#endif
bool x11Display(const std::string &display);
void prepareRenderer();
class Controller final {
public:
    explicit Controller(std::unique_ptr<Backend> backend = {});
    ~Controller();
    Controller(const Controller &) = delete;
    Controller &operator=(const Controller &) = delete;
    void configure(RendererMode mode, fcitx::EventLoop *loop, SizeOptions size = {});
    bool eligible(fcitx::InputContext *context) const;
    bool canUpdate(fcitx::InputContext *context) const;
    bool renderFrame(fcitx::InputContext *context, const nlohmann::json &frame,
                     FrameIdentity identity, std::function<bool()> valid,
                     std::function<void(int)> action,
                     std::function<void(const nlohmann::json &)> acknowledge,
                     std::function<void()> fallback, std::chrono::steady_clock::time_point ready,
                     bool systemDark = false, double systemTextScale = 1.0,
                     bool textScaleKnown = false, const FocusIdentity &focusIdentity = {},
                     std::function<void()> recover = {});
    /// pending 与实际曝光分开；兼容现有同步 XCB 调用入口。
    Submission submission() const;
    void registerCallback(fcitx::InputContext *context);
    void hide(fcitx::InputContext *context);
    void invalidate(fcitx::InputContext *context);
    bool active() const { return active_; }
    bool accepts(const FrameIdentity &identity) const;
    void refresh(fcitx::InputContext *context);
private:
    const char *rejection(fcitx::InputContext *context) const;
    bool unavailable(const char *reason);
    void fail(fcitx::InputContext *context);
    bool click(int x, int y, unsigned button);
    bool pollEvents(fcitx::InputContext *context, uint64_t submission, fcitx::IOEventFlags flags);
    bool checkHealth(fcitx::InputContext *context, uint64_t submission, fcitx::EventSourceTime *timer, uint64_t now);
    void releaseResult();
    bool geometryChanged();
    void redrawGeometry(fcitx::InputContext *context);
    void discardFrame(fcitx::InputContext *context, bool withdraw);
    /// 当前输入帧开始 UI 处理的时间，不包含 IPC 等待。
    std::chrono::steady_clock::time_point ready_;

    /// 未验收场景不参与 auto。
    RendererMode mode_ = RendererMode::Fcitx;

    SizeOptions size_;

    /// 只记录尺寸变化，不在每次按键刷诊断；不含文字或光标位置。
    std::string sizeDiagnostic_;

    /// Fcitx 主线程事件循环，生命期长于上下文。
    fcitx::EventLoop *loop_ = nullptr;

    /// 窗口随上下文销毁。
    std::shared_ptr<Backend> backend_;

#if defined(QJ_GNOME_BACKEND)
    std::unique_ptr<GnomePanel> gnomePanel_;
#endif

#if defined(QJ_GNOME_PROBE)
    /// 隔离测试专用，不参与生产自动选择。
    std::unique_ptr<GnomeProbe> gnomeProbe_;
#endif

    /// 后端绑定的上下文 display；上下文迁移后重新建窗。
    std::string display_;

    /// fd 可读事件先于后端销毁。
    std::unique_ptr<fcitx::EventSourceIO> io_;

    /// 检查没有 X 事件的合成器 selection 丢失；隐藏时禁用。
    std::unique_ptr<fcitx::EventSourceTime> health_;

    /// 输出异步更新期间撤下旧窗口，有界等待 XWayland 与 Shell 几何一致。
    std::unique_ptr<fcitx::EventSourceTime> geometryTimer_;

    std::vector<fcitx::Rect> shellMonitors_;

    /// 出错的生产连接在下一帧重建；测试注入承载由夹具恢复。
    bool backendFailed_ = false;

    bool injected_ = false;

    /// 本地提交代次，覆盖同一 Server identity 的重绘及同步事件回调。
    uint64_t submission_ = 0;

    /// 结果在窗口贴图与鼠标命中结束后由 Rust 释放。
    void *result_ = nullptr;

    /// 当前帧完整身份。
    FrameIdentity identity_;

    /// 窗口是否已提交当前帧。
    bool active_ = false;

    /// 同一原因仅记录一次，不含候选或拼音内容。
    std::string failure_;

    /// 当前结果是否允许请求上一页和下一页。
    bool previous_ = false, next_ = false;

    /// 验证焦点、当前输入法会话、隐私边界与 revision。
    std::function<bool()> valid_;

    /// 已有选词和翻页入口。
    std::function<void(int)> action_;

    /// 仅在贴图成功后报告完整义项。
    std::function<void(const nlohmann::json &)> acknowledge_;

    /// 渲染或窗口出错时重启现有默认面板更新。
    std::function<void()> fallback_;

    std::function<void()> recover_;

    /// 借用已初始化渲染器；销毁/隐私边界不触发新的字体扫描。
    std::function<void()> clearTextCache_;
};
}
