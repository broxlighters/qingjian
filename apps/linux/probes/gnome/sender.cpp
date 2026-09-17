//! 验证 Fcitx 原生 D-Bus 绑定的密封 memfd 发送；不连接 X11 或输入法 Server。
#include <fcitx-utils/dbus/bus.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/unixfd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    constexpr const char *service = "org.qingjian.PanelProbe1";
    constexpr const char *path = "/org/qingjian/PanelProbe1";
    // 超过 JS 精确整数上限，用十进制字符串检查完整往返。
    const std::string identity = "18446744073709551614";
    constexpr uint32_t width = 320, height = 96, stride = width * 4;
    std::vector<uint8_t> pixels(stride * height);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const auto offset = y * stride + x * 4;
            pixels[offset] = x < width / 2 ? 32 : 192;
            pixels[offset + 1] = y < height / 2 ? 160 : 64;
            pixels[offset + 2] = 96;
            pixels[offset + 3] = 255;
        }
    }
    auto fd = fcitx::UnixFD::own(memfd_create("qingjian-probe", MFD_CLOEXEC | MFD_ALLOW_SEALING));
    if (fd.fd() < 0) return 1;
    size_t offset = 0;
    while (offset < pixels.size()) {
        const auto written = write(fd.fd(), pixels.data() + offset, pixels.size() - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return 1;
        offset += written;
    }
    if (lseek(fd.fd(), 0, SEEK_SET) != 0 ||
        fcntl(fd.fd(), F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL) < 0) return 1;
    fcitx::EventLoop loop;
    fcitx::dbus::Bus bus(fcitx::dbus::BusType::Session);
    if (!bus.isOpen()) return 1;
    bus.attachEventLoop(&loop);
    bool success = false;
    std::vector<std::unique_ptr<fcitx::dbus::Slot>> pending;
    auto painted = bus.addMatch(fcitx::dbus::MatchRule(service, path, service, "Painted"),
        [&](fcitx::dbus::Message &signal) {
            std::string received;
            signal >> received;
            if (!signal || received != identity) return true;
            auto hide = bus.createMethodCall(service, path, service, "Hide");
            pending.push_back(hide.callAsync(1000000, [&](fcitx::dbus::Message &reply) {
                success = !reply.isError();
                loop.exit();
                return true;
            }));
            return true;
        });
    auto hello = bus.createMethodCall(service, path, service, "Hello");
    hello << uint32_t(1);
    pending.push_back(hello.callAsync(1000000, [&](fcitx::dbus::Message &reply) {
        uint32_t version = 0;
        if (!reply.isError()) reply >> version;
        if (!reply || version != 1) { std::cerr << "握手失败\n"; loop.exit(); return true; }
        auto prepare = bus.createMethodCall(service, path, service, "Prepare");
        prepare << identity << fd << width << height << stride << uint32_t(pixels.size());
        pending.push_back(prepare.callAsync(1500000, [&](fcitx::dbus::Message &prepared) {
            uint32_t length = 0;
            if (!prepared.isError()) prepared >> length;
            if (!prepared || length != pixels.size()) {
                std::cerr << "FD 读取或纹理上传失败\n";
                loop.exit();
                return true;
            }
            auto show = bus.createMethodCall(service, path, service, "Show");
            show << identity;
            pending.push_back(show.callAsync(1000000, [&](fcitx::dbus::Message &shown) {
                if (shown.isError()) { std::cerr << "Show 失败\n"; loop.exit(); }
                return true;
            }));
            return true;
        }));
        return true;
    }));
    auto timeout = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 4000000, 0,
        [&](fcitx::EventSourceTime *, uint64_t) {
            std::cerr << "绘制回执超时\n";
            loop.exit();
            return false;
        });
    loop.exec();
    if (success) std::cout << "通过：Fcitx UnixFD → GJS 异步读取 → St.ImageContent → after-paint → Hide；64 位身份无损。\n";
    return success ? 0 : 1;
}
