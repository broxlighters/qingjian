//! 异步读取桌面外观；只消费 portal 通知，不在按键路径等待 D-Bus。
#pragma once
#include <fcitx-utils/dbus/bus.h>
#include <fcitx-utils/dbus/variant.h>
#include <functional>
#include <memory>
namespace qingjian::panel {
class Appearance final {
public:
    Appearance(fcitx::EventLoop &loop, std::function<void()> changed);
    bool dark() const { return dark_; }
    double textScale() const { return textScale_; }
    bool textScaleKnown() const { return textScaleKnown_; }
private:
    void update(const fcitx::dbus::Variant &value);
    void updateTextScale(const fcitx::dbus::Variant &value);
    /// 没有偏好或 portal 不可用时采用浅色。
    bool dark_ = false;

    /// portal 未提供文字偏好时不猜测 Xft DPI 中的组成。
    double textScale_ = 1.0;

    bool textScaleKnown_ = false;

    /// 仅在颜色发生变化时刷新当前有效帧。
    std::function<void()> changed_;

    /// 插件侧连接，随引擎销毁；slot 先于总线析构。
    std::unique_ptr<fcitx::dbus::Bus> bus_;

    /// 外观变化订阅。
    std::unique_ptr<fcitx::dbus::Slot> signal_;

    /// 初始化异步请求。
    std::unique_ptr<fcitx::dbus::Slot> pending_;

    std::unique_ptr<fcitx::dbus::Slot> textSignal_;

    std::unique_ptr<fcitx::dbus::Slot> textPending_;
};
}
