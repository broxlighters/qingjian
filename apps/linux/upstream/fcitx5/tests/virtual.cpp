//! 使用真实 VirtualInputContextManager 覆盖委托上下文匹配与父对象寿命。
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "virtual.h"
#include <cassert>
#include "virtualinputcontext.h"
#include "appmonitor.h"
#include <fcitx/inputcontextmanager.h>
namespace {
class Monitor : public fcitx::AppMonitor {
public:
    bool isAvailable() const override { return true; }
};
}
struct VirtualFixture::Data {
    Monitor monitor;
    fcitx::VirtualInputContextGlue *parent;
    std::unique_ptr<fcitx::VirtualInputContextManager> manager;
};
VirtualFixture::VirtualFixture(fcitx::InputContext *parent, fcitx::InputContextManager &manager) : data_(std::make_unique<Data>()) {
    data_->parent = dynamic_cast<fcitx::VirtualInputContextGlue *>(parent);
    assert(data_->parent);
    data_->manager = std::make_unique<fcitx::VirtualInputContextManager>(
        &manager, data_->parent, &data_->monitor);
}
VirtualFixture::~VirtualFixture() = default;
fcitx::InputContext *VirtualFixture::focus() {
    data_->monitor.appUpdated({{"test-app", "test-app"}}, std::string("test-app"));
    data_->manager->setRealFocus(true);
    auto *ic = data_->parent->delegatedInputContext();
    assert(ic != data_->parent);
    return ic;
}
void VirtualFixture::clear() {
    data_->monitor.appUpdated({}, std::nullopt);
}

// frontend 字符串相同也不能把其它 glue 父类型当作 v2。
#include <waylandim_popup_public.h>
namespace {
class WrongParent : public fcitx::VirtualInputContextGlue {
public:
    explicit WrongParent(fcitx::InputContextManager &manager) : VirtualInputContextGlue(manager) { created(); }
    ~WrongParent() override { destroy(); }
    const char *frontend() const override { return "wayland_v2"; }
    void commitStringDelegate(const fcitx::InputContext *, const std::string &) const override {}
    void deleteSurroundingTextDelegate(fcitx::InputContext *, int, unsigned) const override {}
    void forwardKeyDelegate(fcitx::InputContext *, const fcitx::ForwardKeyEvent &) const override {}
    void updatePreeditDelegate(fcitx::InputContext *) const override {}
};
}
void testWrongVirtualParent(fcitx::AddonInstance *addon, fcitx::InputContextManager &manager) {
    WrongParent parent(manager);
    fcitx::VirtualInputContext child(manager, "fake-v2", &parent);
    assert(addon->call<fcitx::IWaylandIMModule::queryPopup>(&child) == fcitx::WaylandPopupAvailability::InvalidContext);
    assert(!addon->call<fcitx::IWaylandIMModule::createPopup>(&child, [](auto) { assert(false); }));
}
