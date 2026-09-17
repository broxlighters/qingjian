//! 所有后端共用展示状态；接受提交、软件绘制和撤窗不能混为一次布尔成功。
#pragma once
namespace qingjian::panel {
enum class DisplayState { Idle, Preparing, AwaitingPaint, Visible, Hiding, Fallback };
}
