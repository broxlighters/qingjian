//! 仅在夹具创建的独立 Xvfb 上接管测试 selection，验证合成器消失与服务端超时。
#include "panel/backend/xcb.h"
#include <xcb/xcb.h>
#include <cassert>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
using Clock = std::chrono::steady_clock;
int main(int argc, char **argv) {
    assert(argc == 3);
    auto *connection = xcb_connect(argv[1], nullptr);
    assert(connection && !xcb_connection_has_error(connection));
    const auto *screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    const auto owner = xcb_generate_id(connection);
    xcb_create_window(connection, XCB_COPY_FROM_PARENT, owner, screen->root, 0, 0, 1, 1, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0, nullptr);
    const auto selection = std::string("_NET_WM_CM_S0");
    const std::unique_ptr<xcb_intern_atom_reply_t, decltype(&std::free)> atom(xcb_intern_atom_reply(connection,
        xcb_intern_atom(connection, false, selection.size(), selection.c_str()), nullptr), &std::free);
    assert(atom);
    auto select = [&](xcb_window_t window) {
        xcb_set_selection_owner(connection, window, atom->atom, XCB_CURRENT_TIME);
        const std::unique_ptr<xcb_get_selection_owner_reply_t, decltype(&std::free)> current(xcb_get_selection_owner_reply(connection,
            xcb_get_selection_owner(connection, atom->atom), nullptr), &std::free);
        assert(current && current->owner == window);
    };
    select(owner);
    auto backend = qingjian::panel::openXcb(argv[1]);
    assert(backend && backend->healthy());
    // 此处只有隔离 Xvfb，工作区事件应通知重绘而不使连接失败。
    const std::string desktop = "_NET_CURRENT_DESKTOP";
    const std::unique_ptr<xcb_intern_atom_reply_t, decltype(&std::free)> desktopAtom(xcb_intern_atom_reply(connection,
        xcb_intern_atom(connection, false, desktop.size(), desktop.c_str()), nullptr), &std::free);
    assert(desktopAtom);
    const uint32_t currentDesktop = 0;
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, screen->root, desktopAtom->atom,
        XCB_ATOM_CARDINAL, 32, 1, &currentDesktop);
    xcb_flush(connection);
    bool changed = false;
    auto geometryDeadline = Clock::now() + std::chrono::seconds(1);
    while (!changed && Clock::now() < geometryDeadline) {
        assert(backend->poll([](int, int, unsigned) { assert(false); return false; }));
        changed = backend->takeGeometryChanged();
        if (!changed) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(changed && !backend->takeGeometryChanged() && backend->healthy());
    const uint8_t pixels[16] = {};
    assert(backend->present(pixels, 2, 2, 8, fcitx::Rect(10, 10, 11, 30)));
    select(XCB_NONE);
    auto deadline = Clock::now() + std::chrono::seconds(2);
    while (backend->healthy() && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(!backend->healthy()); // 已失效的承载不能在后续帧假装恢复。
    backend.reset();
    select(owner);
    backend = qingjian::panel::openXcb(argv[1]);
    assert(backend);
    // shell 的退出 trap 会恢复并终止该测试子进程，即使断言失败也不会留下 STOP。
    const auto server = static_cast<pid_t>(std::stoi(argv[2]));
    assert(server > 1 && kill(server, SIGSTOP) == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(260));
    auto started = Clock::now();
    const bool pending = backend->healthy();
    const auto queryDuration = Clock::now() - started;
    std::this_thread::sleep_for(std::chrono::milliseconds(1050));
    started = Clock::now();
    const bool timedOut = !backend->healthy();
    const auto timeoutDuration = Clock::now() - started;
    assert(kill(server, SIGCONT) == 0);
    assert(pending && timedOut && queryDuration < std::chrono::milliseconds(100) && timeoutDuration < std::chrono::milliseconds(100));
    backend.reset();
    xcb_disconnect(connection);
}
