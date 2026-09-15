//! 生命周期跟随 InputContext 的 IPC 与中英模式状态。
#pragma once
#include "ipc/connection.h"
#include <fcitx/inputcontextproperty.h>
namespace qingjian {
class Session final : public fcitx::InputContextProperty {
public:
    ~Session() override { connection.close(); }
    /// 一条连接只使用一个本地编号，Server 会重新映射全局编号。
    static constexpr uint64_t id = 1;
    /// 传输连接；销毁时断线让 Server 回收会话。
    Connection connection;
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
};
}
