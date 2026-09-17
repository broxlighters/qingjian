//! 阶段 0 组件夹具的指定程序与 dbus 相对坐标上下文。
#pragma once
#include <fcitx/inputcontext.h>
#include <fcitx-utils/dbus/bus.h>
class GnomeContext final : public fcitx::InputContext, public fcitx::dbus::ObjectVTable<GnomeContext> {
public:
    explicit GnomeContext(fcitx::InputContextManager &manager, fcitx::dbus::Bus *bus = nullptr) : InputContext(manager, "probe-test") {
        created();
        if (bus) bus->addObjectVTable("/org/freedesktop/portal/inputcontext/1", "org.fcitx.Fcitx.InputContext1", *this);
    }
    ~GnomeContext() override { destroy(); }
    const char *frontend() const override { return "dbus"; }
protected:
    void commitStringImpl(const std::string &) override {}
    void deleteSurroundingTextImpl(int, unsigned int) override {}
    void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
    void updatePreeditImpl() override {}
};
