//! 输入上下文所属显示协议；不从桌面环境变量推断。
#pragma once
namespace qingjian::panel {
enum class DisplayKind { Unknown, X11, Wayland };
}
