//! Fcitx5 壳：上下文属性、输入事件与候选 UI。
#pragma once
#include "session.h"
#include "panel/appearance.h"
#include <fcitx/addonfactory.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/instance.h>
#include <vector>
#if defined(QJ_GNOME_BACKEND)
namespace qingjian::panel { class GnomeBridge; }
#endif
namespace fcitx {
class QingjianEngine final : public InputMethodEngineV2 {
public:
    explicit QingjianEngine(AddonManager *manager);
    void keyEvent(const InputMethodEntry &, KeyEvent &) override;
    void reset(const InputMethodEntry &, InputContextEvent &) override;
    void deactivate(const InputMethodEntry &, InputContextEvent &) override;
    bool process(InputContext *context, const Key &key);
private:
    void clear(InputContext *context);
    void disconnect(InputContext *context);
    bool syncPrivacy(InputContext *context);
    bool commitRaw(InputContext *context, bool deliver);
    void render(InputContext *context, const nlohmann::json &frame);
    /// Fcitx 实例，负责 UI flush 和窗口事件。
    Instance *instance_;
#if defined(QJ_GNOME_BACKEND)
    /// 插件初始化时提前协商，保留实例级连接供所有上下文共用。
    std::shared_ptr<qingjian::panel::GnomeBridge> gnomeBridge_;
#endif
    /// 不复制组句；Fcitx 自动随 InputContext 回收属性。
    FactoryFor<qingjian::Session> sessions_;
    /// 桌面主题异步更新，只影响 system 外观的自绘帧。
    std::unique_ptr<qingjian::panel::Appearance> appearance_;
    /// 能力变化时立即清理面板，避免旧私密 preedit 被 Fcitx 失焦自动提交。
    std::unique_ptr<HandlerTableEntry<EventHandler>> capabilityWatcher_;
    /// 光标移动时重新提交当前有效展示帧。
    std::unique_ptr<HandlerTableEntry<EventHandler>> cursorWatcher_;
    /// 虚拟键盘优先接管时同步撤下自绘窗口。
    std::unique_ptr<HandlerTableEntry<EventHandler>> virtualKeyboardWatcher_;

    /// 阶段 0 焦点/坐标证据；只有明确启用诊断时才写日志。
    std::vector<std::unique_ptr<HandlerTableEntry<EventHandler>>> diagnosticWatchers_;
};
}
