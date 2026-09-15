//! Fcitx5 壳：上下文属性、输入事件与候选 UI。
#pragma once
#include "session.h"
#include <fcitx/addonfactory.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/instance.h>
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
    /// 不复制组句；Fcitx 自动随 InputContext 回收属性。
    FactoryFor<qingjian::Session> sessions_;
    /// 能力变化时立即清理面板，避免旧私密 preedit 被 Fcitx 失焦自动提交。
    std::unique_ptr<HandlerTableEntry<EventHandler>> capabilityWatcher_;
};
}
