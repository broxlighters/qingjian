//! 生命周期跟随 InputContext 的 IPC 与中英模式状态。
#pragma once
#include "ipc/connection.h"
#include "panel/controller.h"
#include <fcitx/inputcontextproperty.h>
namespace qingjian {
class Session final : public fcitx::InputContextProperty {
public:
    ~Session() override { socketWatcher.reset(); connection.close(); }
    /// 一条连接只使用一个本地编号，Server 会重新映射全局编号。
    static constexpr uint64_t id = 1;
    /// 传输连接；销毁时断线让 Server 回收会话。
    Connection connection;
    /// Server 空闲时退出也立即清窗，不等下一次按键。
    std::unique_ptr<fcitx::EventSourceIO> socketWatcher;
    /// 成功握手。
    bool opened = false;
    /// 最近向 Server 确认的隐私状态；新会话 Server 默认 private。
    bool privateInput = true;
    /// 最近一帧是否放入了 clientPreedit。FocusOut 时 Fcitx ReservedFirst 负责提交这一份。
    bool clientPreedit = false;
    /// 持久英文模式。
    bool english = false;
    /// 单击 Shift；出现其他按键便取消候选切换。
    bool shiftPending = false;
    /// 当前候选帧的版本；旧鼠标回调不能选中新一帧。
    uint64_t revision = 0;
    /// 同一 InputContext 的连接代次，断线重连后旧帧全部失效。
    uint64_t generation = 0;
    /// 新 Server 已协商 Linux UI 扩展，旧 Server 继续默认面板。
    bool displayReporting = false;
    /// 服务端给出的完整帧身份。
    nlohmann::json displayIdentity;
    /// 当前有效帧，仅供移动光标时刷新；隐私/失焦/断线时清除。
    nlohmann::json lastFrame;
    /// Server 下发的拼音位置；legacy 保留旧协议的显示行为。
    std::string preeditMode = "legacy";
    /// Linux 自绘面板控制器；不可用时保持默认 Fcitx 面板。
    panel::Controller panel;
};
}
