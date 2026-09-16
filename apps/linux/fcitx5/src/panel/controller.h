//! 青简上下文的面板接管、帧验证、窗口与 Rust 结果生命周期。
#pragma once
#include "identity.h"
#include "mode.h"
#include "backend/base.h"
#include <fcitx-utils/event.h>
#include <nlohmann/json.hpp>
#include <functional>
#include <chrono>
#include <memory>
#include <string>
namespace fcitx { class InputContext; }
namespace qingjian::panel {
bool x11Display(const std::string &display);
void prepareRenderer();
class Controller final {
public:
    explicit Controller(std::unique_ptr<Backend> backend = {});
    ~Controller();
    Controller(const Controller &) = delete;
    Controller &operator=(const Controller &) = delete;
    void configure(RendererMode mode, fcitx::EventLoop *loop);
    bool eligible(fcitx::InputContext *context) const;
    bool renderFrame(fcitx::InputContext *context, const nlohmann::json &frame,
                     FrameIdentity identity, std::function<bool()> valid,
                     std::function<void(int)> action,
                     std::function<void(const nlohmann::json &)> acknowledge,
                     std::function<void()> fallback, std::chrono::steady_clock::time_point ready,
                     bool systemDark = false);
    void registerCallback(fcitx::InputContext *context);
    void hide(fcitx::InputContext *context);
    void invalidate(fcitx::InputContext *context);
    bool active() const { return active_; }
    bool accepts(const FrameIdentity &identity) const;
    void refresh(fcitx::InputContext *context);
private:
    bool unavailable(const char *reason);
    void fail(fcitx::InputContext *context);
    bool click(int x, int y, unsigned button);
    void releaseResult();
    /// 当前输入帧开始 UI 处理的时间，不包含 IPC 等待。
    std::chrono::steady_clock::time_point ready_;

    /// 未验收场景不参与 auto。
    RendererMode mode_ = RendererMode::Fcitx;

    /// Fcitx 主线程事件循环，生命期长于上下文。
    fcitx::EventLoop *loop_ = nullptr;

    /// 窗口随上下文销毁。
    std::unique_ptr<Backend> backend_;

    /// 后端绑定的 X display；上下文迁移后重新建窗。
    std::string display_;

    /// fd 可读事件先于后端销毁。
    std::unique_ptr<fcitx::EventSourceIO> io_;

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
};
}
