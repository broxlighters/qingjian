//! Settings.Read 兼容 portal v1 的双层 variant，SettingChanged 使用单层 variant。
#include "appearance.h"
#include <exception>
#include <cmath>
namespace qingjian::panel {
Appearance::Appearance(fcitx::EventLoop &loop, std::function<void()> changed)
    : changed_(std::move(changed)) {
    try {
        bus_ = std::make_unique<fcitx::dbus::Bus>(fcitx::dbus::BusType::Session);
        if (!bus_->isOpen()) return;
        bus_->attachEventLoop(&loop);
        fcitx::dbus::registerVariantType<fcitx::dbus::Variant>();
        fcitx::dbus::registerVariantType<double>();
        signal_ = bus_->addMatch(fcitx::dbus::MatchRule(
            "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
            "org.freedesktop.portal.Settings", "SettingChanged",
            {"org.freedesktop.appearance", "color-scheme"}),
            [this](fcitx::dbus::Message &message) {
                std::string space, key;
                fcitx::dbus::Variant value;
                message >> space >> key >> value;
                if (message && space == "org.freedesktop.appearance" && key == "color-scheme") update(value);
                return true;
            });
        auto request = bus_->createMethodCall("org.freedesktop.portal.Desktop",
            "/org/freedesktop/portal/desktop", "org.freedesktop.portal.Settings", "Read");
        request << std::string("org.freedesktop.appearance") << std::string("color-scheme");
        pending_ = request.callAsync(1000000, [this](fcitx::dbus::Message &reply) {
            if (!reply.isError()) {
                fcitx::dbus::Variant value;
                reply >> value;
                if (reply) update(value);
            }
            return true;
        });
        textSignal_ = bus_->addMatch(fcitx::dbus::MatchRule(
            "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
            "org.freedesktop.portal.Settings", "SettingChanged",
            {"org.gnome.desktop.interface", "text-scaling-factor"}),
            [this](fcitx::dbus::Message &message) {
                std::string space, key;
                fcitx::dbus::Variant value;
                message >> space >> key >> value;
                if (message && space == "org.gnome.desktop.interface" && key == "text-scaling-factor") updateTextScale(value);
                return true;
            });
        auto textRequest = bus_->createMethodCall("org.freedesktop.portal.Desktop",
            "/org/freedesktop/portal/desktop", "org.freedesktop.portal.Settings", "Read");
        textRequest << std::string("org.gnome.desktop.interface") << std::string("text-scaling-factor");
        textPending_ = textRequest.callAsync(1000000, [this](fcitx::dbus::Message &reply) {
            if (!reply.isError()) {
                fcitx::dbus::Variant value;
                reply >> value;
                if (reply) updateTextScale(value);
            }
            return true;
        });
    } catch (const std::exception &) {
        textPending_.reset();
        textSignal_.reset();
        pending_.reset();
        signal_.reset();
        bus_.reset();
    }
}
void Appearance::update(const fcitx::dbus::Variant &value) {
    const auto *unwrapped = &value;
    if (value.signature() == "v") unwrapped = &value.dataAs<fcitx::dbus::Variant>();
    if (unwrapped->signature() != "u") return;
    const bool dark = unwrapped->dataAs<uint32_t>() == 1;
    if (dark_ == dark) return;
    dark_ = dark;
    if (changed_) changed_();
}
void Appearance::updateTextScale(const fcitx::dbus::Variant &value) {
    const auto *unwrapped = &value;
    if (value.signature() == "v") unwrapped = &value.dataAs<fcitx::dbus::Variant>();
    if (unwrapped->signature() != "d") return;
    const auto scale = unwrapped->dataAs<double>();
    if (!std::isfinite(scale) || scale < 0.5 || scale > 3.0) return;
    const bool changed = !textScaleKnown_ || textScale_ != scale;
    textScale_ = scale;
    textScaleKnown_ = true;
    if (changed && changed_) changed_();
}
}
