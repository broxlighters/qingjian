//! 真实 wayland/waylandim addon、Fcitx 单 reader 与内存 compositor 的集成测试。
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/testing.h>
#include <fcitx-config/rawconfig.h>
#include <fcitx/addonmanager.h>
#include <fcitx/event.h>
#include <fcitx/focusgroup.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/instance.h>
#include <wayland_public.h>
#include <waylandim_popup_public.h>
#include "compositor.h"
#include "virtual.h"
using A = fcitx::WaylandPopupAvailability;
using R = fcitx::WaylandPopupCloseReason;

class WrongContext : public fcitx::InputContext {
public:
    WrongContext(fcitx::InputContextManager &manager, const char *frontend)
        : InputContext(manager), frontend_(frontend) { created(); }
    ~WrongContext() override { destroy(); }
    const char *frontend() const override { return frontend_; }
private:
    void commitStringImpl(const std::string &) override {}
    void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
    void deleteSurroundingTextImpl(int, unsigned) override {}
    void updatePreeditImpl() override {}
    const char *frontend_;
};

int main(int argc, char **args) {
    const std::string mode = argc > 1 ? args[1] : "lifecycle";
    setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/popup-test-bus", 1);
    setenv("GIO_USE_VFS", "local", 1);
    unsetenv("WAYLAND_DISPLAY");
    unsetenv("WAYLAND_SOCKET");
    unsetenv("DISPLAY");
    unsetenv("XDG_CURRENT_DESKTOP");
    setenv("XDG_SESSION_TYPE", "tty", 1);
    fcitx::setupTestingEnvironmentPath(FCITX_BUILD, {"bin"}, {TEST_DATA});
    char arg0[] = "popup-lifecycle";
    char arg1[] = "--disable=all";
    char arg2[] = "--enable=wayland,waylandim";
    char arg3[] = "--option=wayland=nodefault";
    char arg4[] = "-k";
    char *argv[] = {arg0, arg1, arg2, arg3, arg4};
    fcitx::Instance instance(5, argv);
    instance.addonManager().registerDefaultLoader(nullptr);
    Compositor compositor(instance.eventLoop(), mode != "missing");
    fcitx::AddonInstance *addon = nullptr;
    fcitx::AddonInstance *wayland = nullptr;
    fcitx::InputContext *ic = nullptr;
    std::unique_ptr<fcitx::WaylandPopup> popup;
    std::unique_ptr<VirtualFixture> virtualIC;
    wl_callback *frame = nullptr;
    wl_registry *registry = nullptr;
    wl_shm *shm = nullptr;
    wl_buffer *buffer = nullptr;
    wl_display *borrowed = nullptr;
    void *pixels = MAP_FAILED;
    int closeCount = 0;
    R last = R::Closed;
    int stage = 0;
    auto query = [&] { return addon->call<fcitx::IWaylandIMModule::queryPopup>(ic); };
    auto create = [&] {
        assert(query() == A::Available);
        popup = addon->call<fcitx::IWaylandIMModule::createPopup>(ic, [&](R reason) {
            assert(popup && !popup->surface() && !popup->display());
            if (reason != R::Closed) {
                assert(addon->call<fcitx::IWaylandIMModule::queryPopup>(ic) == A::Unavailable);
                assert(!addon->call<fcitx::IWaylandIMModule::createPopup>(ic, [](R) { assert(false); }));
            }
            ++closeCount;
            last = reason;
            if (frame) { wl_callback_destroy(frame); frame = nullptr; }
            if (buffer) { wl_buffer_destroy(buffer); buffer = nullptr; }
            if (shm) { wl_shm_destroy(shm); shm = nullptr; }
            if (registry) { wl_registry_destroy(registry); registry = nullptr; }
            if (pixels != MAP_FAILED) { munmap(pixels, 4096); pixels = MAP_FAILED; }
            popup->close(); // 幂等重入。
            popup.reset(); // 在失效调用栈中释放句柄。
        });
        assert(popup && popup->surface() && popup->display());
    };
    instance.eventDispatcher().schedule([&] {
        addon = instance.addonManager().addon("waylandim");
        wayland = instance.addonManager().addon("wayland");
        assert(addon && wayland);
        fcitx::RawConfig config;
        config.setValueByPath("DetectApplication", "False");
        addon->setConfig(config);
        assert(addon->call<fcitx::IWaylandIMModule::queryPopup>(nullptr) == A::InvalidContext);
        for (const char *frontend : {"wayland", "ibus", "wayland_v2"}) {
            WrongContext wrong(instance.inputContextManager(), frontend);
            assert(addon->call<fcitx::IWaylandIMModule::queryPopup>(&wrong) ==
                (std::string(frontend) == "wayland_v2" ? A::InvalidContext : A::UnsupportedFrontend));
            assert(!addon->call<fcitx::IWaylandIMModule::createPopup>(&wrong, [](R) { assert(false); }));
        }
        testWrongVirtualParent(addon, instance.inputContextManager());
        assert(wayland->call<fcitx::IWaylandModule::reopenConnectionSocket>("", compositor.takeClientFd()));
    });
    const auto start = fcitx::now(CLOCK_MONOTONIC);
    auto timer = instance.eventLoop().addTimeEvent(CLOCK_MONOTONIC, start, 1000,
        [&](fcitx::EventSourceTime *timer, uint64_t) {
            if (fcitx::now(CLOCK_MONOTONIC) - start > 5000000) { std::cerr << "timeout stage=" << stage << " im=" << compositor.im << " surfaces=" << compositor.surfaces << " roles=" << compositor.roles << "\n"; instance.exit(); return true; }
            timer->setNextInterval(1000);
            timer->setOneShot();
            if (!addon) { return true; }
            switch (stage) {
            case 0:
                if (!compositor.im) { break; }
                instance.inputContextManager().foreach([&](fcitx::InputContext *candidate) {
                    if (candidate->frontendName() == "wayland_v2") { ic = candidate; }
                    return true;
                });
                assert(ic && ic->display() == "wayland:");
                assert(query() == A::Inactive);
                assert(!addon->call<fcitx::IWaylandIMModule::createPopup>(ic, [](R) { assert(false); }));
                compositor.activate();
                ++stage;
                break;
            case 1:
                if (mode == "missing") {
                    if (!ic->hasFocus()) { break; }
                    assert(query() == A::MissingCompositor);
                    assert(!addon->call<fcitx::IWaylandIMModule::createPopup>(ic, [](R) { assert(false); }));
                    stage = 91;
                    instance.exit();
                    break;
                }
                if (query() != A::Available) { break; }
                assert(!addon->call<fcitx::IWaylandIMModule::createPopup>(ic, fcitx::WaylandPopupClosed{}));
                create();
                borrowed = popup->display();
                assert(wl_surface_get_user_data(popup->surface()));
                frame = wl_surface_frame(popup->surface());
                registry = wl_display_get_registry(borrowed);
                {
                    static const wl_registry_listener listener = {
                        [](void *data, wl_registry *r, uint32_t id, const char *name, uint32_t) {
                            if (std::string(name) == "wl_shm") {
                                *static_cast<wl_shm **>(data) = static_cast<wl_shm *>(wl_registry_bind(r, id, &wl_shm_interface, 1));
                            }
                        }, [](void *, wl_registry *, uint32_t) {}
                    };
                    wl_registry_add_listener(registry, &listener, &shm);
                }
                ++stage;
                break;
            case 2:
                if (!shm || !compositor.roles) { break; }
                {
                    int fd = memfd_create("popup-test", MFD_CLOEXEC);
                    assert(fd >= 0 && ftruncate(fd, 4096) == 0);
                    pixels = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                    assert(pixels != MAP_FAILED);
                    *static_cast<uint32_t *>(pixels) = 0xff00ff00;
                    auto *pool = wl_shm_create_pool(shm, fd, 4096);
                    buffer = wl_shm_pool_create_buffer(pool, 0, 1, 1, 4, WL_SHM_FORMAT_ARGB8888);
                    wl_shm_pool_destroy(pool);
                    close(fd);
                    wl_surface_attach(popup->surface(), buffer, 0, 0);
                    wl_surface_damage(popup->surface(), 0, 0, 1, 1);
                    wl_surface_commit(popup->surface());
                }
                ++stage;
                break;
            case 3:
                if (!compositor.commits || !compositor.frames) { break; }
                if (mode == "disconnect") {
                    compositor.disconnect();
                    stage = 90;
                    break;
                }
                if (mode == "unload") {
                    instance.addonManager().unload();
                    assert(last == R::ModuleUnloaded && !popup && !frame && !buffer);
                    stage = 91;
                    instance.exit();
                    break;
                }
                if (mode == "global") {
                    compositor.removeCompositor();
                    stage = 90;
                    break;
                }
                popup->close();
                assert(closeCount == 1 && last == R::Closed && !popup && !frame && !buffer);
                ++stage;
                break;
            case 4:
                if (compositor.roles || compositor.surfaces) { break; }
                assert(compositor.hidden == 1);
                create();
                ic->focusOut();
                assert(!popup && last == R::ContextChanged && query() == A::Inactive);
                ic->focusIn();
                for (auto flag : {fcitx::CapabilityFlag::Password, fcitx::CapabilityFlag::Sensitive,
                                  fcitx::CapabilityFlag::Disable, fcitx::CapabilityFlag::ClientSideInputPanel}) {
                    auto old = ic->capabilityFlags();
                    create();
                    ic->setCapabilityFlags(old | flag);
                    assert(!popup && query() == A::Restricted);
                    ic->setCapabilityFlags(old);
                }
                // 析构句柄不通知；失效句柄 getter 为空且可以重复 close。
                {
                    int notifications = 0;
                    auto other = addon->call<fcitx::IWaylandIMModule::createPopup>(ic, [&](R) { ++notifications; });
                    other.reset();
                    assert(notifications == 0);
                    other = addon->call<fcitx::IWaylandIMModule::createPopup>(ic, [&](R) { ++notifications; });
                    other->close();
                    other->close();
                    assert(notifications == 1 && !other->surface() && !other->display());
                }
                create();
                ic->reset();
                assert(!popup && last == R::ContextChanged);
                {
                    auto *group = ic->focusGroup();
                    fcitx::FocusGroup wrong("wayland:", instance.inputContextManager());
                    ic->setFocusGroup(&wrong);
                    assert(query() == A::DisplayMismatch);
                    ic->setFocusGroup(group);
                    ic->focusIn();
                }
                virtualIC = std::make_unique<VirtualFixture>(ic, instance.inputContextManager());
                {
                    auto *parent = ic;
                    ic = virtualIC->focus();
                    assert(query() == A::Available);
                    assert(addon->call<fcitx::IWaylandIMModule::queryPopup>(parent) == A::Inactive);
                    create();
                    virtualIC->clear();
                    assert(!popup && last == R::ContextChanged);
                    ic = parent;
                }
                virtualIC.reset();
                ic->focusIn();
                create();
                compositor.deactivate();
                ++stage;
                break;
            case 5:
                if (popup) { break; }
                assert(last == R::Deactivated && query() == A::Inactive);
                compositor.activate();
                ++stage;
                break;
            case 6:
                if (query() != A::Available) { break; }
                create();
                // 连续 activate 会作废旧会话。
                compositor.activate();
                ++stage;
                break;
            case 7:
                if (popup) { break; }
                assert(last == R::Deactivated);
                if (query() != A::Available) { break; }
                create();
                compositor.unavailable();
                ++stage;
                break;
            case 8:
                if (popup) { break; }
                assert(last == R::Unavailable && query() != A::Available);
                // unavailable 永久不可用，后续 activate 不能恢复。
                compositor.activate();
                ++stage;
                break;
            case 9:
                assert(query() != A::Available);
                compositor.disconnect();
                ++stage;
                break;
            case 10:
                {
                    bool found = false;
                    instance.inputContextManager().foreach([&](fcitx::InputContext *candidate) {
                        if (candidate->frontendName() == "wayland_v2") { found = true; }
                        return true;
                    });
                    if (found) { break; }
                }
                std::cout << "PASS popup lifecycle: " << closeCount << " close callbacks; single Fcitx reader\n";
                instance.exit();
                ++stage;
                break;
            case 90:
                if (popup) { break; }
                assert(closeCount == 1 && !frame && !buffer && !shm && !registry && pixels == MAP_FAILED);
                if (mode == "disconnect") {
                    assert(last == R::ConnectionClosed);
                } else {
                    assert(last == R::Unavailable && query() == A::MissingCompositor);
                    if (compositor.roles || compositor.surfaces) { break; }
                }
                instance.exit();
                stage = 91;
                break;
            default: break;
            }
            return true;
        });
    timer->setAccuracy(1000);
    int result = instance.exec();
    assert(stage == (mode == "lifecycle" ? 11 : 91));
    std::cout << "PASS " << mode << ": " << closeCount << " close callbacks\n";
    return result;
}
