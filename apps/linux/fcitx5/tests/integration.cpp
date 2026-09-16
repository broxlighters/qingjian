//! 无桌面集成：真实 InputContext、候选面板、按键 IPC、密码框与断线。
#include "qingjian.h"
#include "key/mapping.h"
#include <fcitx/instance.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/inputcontextmanager.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <thread>
using Json = nlohmann::json;

class Context final : public fcitx::InputContext {
public:
    explicit Context(fcitx::InputContextManager &manager) : InputContext(manager, "test") { created(); }
    ~Context() override { destroy(); }
    const char *frontend() const override { return "test"; }
    std::string committed;
protected:
    void commitStringImpl(const std::string &text) override { committed += text; }
    void deleteSurroundingTextImpl(int, unsigned int) override {}
    void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
    void updatePreeditImpl() override {}
};
Json readMessage(int fd) {
    std::array<uint8_t, 4> prefix{};
    assert(recv(fd, prefix.data(), 4, MSG_WAITALL) == 4);
    uint32_t size = uint32_t(prefix[0]) | uint32_t(prefix[1]) << 8 | uint32_t(prefix[2]) << 16 | uint32_t(prefix[3]) << 24;
    std::string body(size, '\0');
    assert(recv(fd, body.data(), size, MSG_WAITALL) == size);
    return Json::parse(body);
}
void writeMessage(int fd, const Json &message) {
    std::string body = message.dump();
    uint32_t size = body.size();
    std::array<uint8_t, 4> prefix{uint8_t(size), uint8_t(size >> 8), uint8_t(size >> 16), uint8_t(size >> 24)};
    assert(send(fd, prefix.data(), 4, MSG_NOSIGNAL) == 4);
    assert(send(fd, body.data(), size, MSG_NOSIGNAL) == size);
}
int main(int argc, char **arguments) {
    const bool reporting = argc > 1;
    const std::string preeditMode = reporting ? arguments[1] : "legacy";
    const std::string rendererMode = argc > 2 ? arguments[2] : "qingjian";
    const bool idleDisconnect = argc > 3;
    auto escaped = qingjian::mapKey(fcitx::Key(FcitxKey_quotedbl), false);
    assert(Json::parse(escaped.dump()).at("character") == "\"");
    assert(qingjian::mapKey(fcitx::Key(FcitxKey_BackSpace), false)["virtual_key"] == 8);
    assert(qingjian::mapKey(fcitx::Key(FcitxKey_Delete), false)["virtual_key"] == 46);
    assert(qingjian::mapKey(fcitx::Key(FcitxKey_a, fcitx::KeyState::Ctrl), false)["modifiers"]["ctrl"] == true);
    char directory[] = "/tmp/qingjian-plugin-XXXXXX";
    assert(mkdtemp(directory));
    std::string socketPath = std::string(directory) + "/server.sock";
    setenv("QINGJIAN_SOCKET", socketPath.c_str(), 1);
    setenv("XDG_CONFIG_HOME", directory, 1);
    setenv("XDG_DATA_HOME", directory, 1);
    int listener = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un address{}; address.sun_family = AF_UNIX;
    std::strcpy(address.sun_path, socketPath.c_str());
    assert(bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    assert(listen(listener, 2) == 0);
    std::thread mock([&] {
        int connection = accept(listener, nullptr, nullptr);
        assert(readMessage(connection).at("OpenSession").at("protocol") == 4);
        Json opened = {{"session", 1}, {"frame", Json::object()}};
        Json identity;
        if (reporting) opened["linux_ui"] = {{"version", 1}};
        writeMessage(connection, {{"Update", opened}});
        if (reporting) {
            identity = readMessage(connection).at("LinuxHello");
            identity.erase("version");
            identity["revision"] = 1;
            writeMessage(connection, {{"LinuxHello", {{"version", 1}, {"renderer", rendererMode}, {"preedit", preeditMode}}}});
        }
        assert(readMessage(connection).at("Privacy").at("private") == false);
        assert(readMessage(connection).at("Key").at("event").at("character") == "n");
        Json frame = Json::parse(R"({"preedit":[{"text":"你'hao","kind":"Typed"}],"cursor":1,"candidates":{"items":[{"text":"","translation":null},{"text":"","translation":null},{"text":"你好","translation":{"senses":[{"text":"hello","fresh":true}]}}]},"highlight":2,"page":0,"page_count":2,"notice":null})");
        Json first = {{"session", 1}, {"outcome", "Consumed"}, {"commit", nullptr}, {"frame", frame}};
        if (!identity.empty()) first["identity"] = identity;
        writeMessage(connection, {{"KeyResult", first}});
        if (!identity.empty()) {
            auto ack = readMessage(connection).at("DisplayAcknowledged");
            assert(ack.at("identity") == identity);
            assert(ack.at("senses") == Json::parse("[[2,0]]"));
        }
        if (idleDisconnect) { close(connection); return; }
        assert(readMessage(connection).at("Privacy").at("private") == false);
        assert(readMessage(connection).at("Key").at("event").at("character") == "3");
        frame["preedit"] = Json::array(); frame["candidates"]["items"] = Json::array(); frame["cursor"] = 0;
        Json second = {{"session", 1}, {"outcome", "Consumed"}, {"commit", "你好"}, {"frame", frame}};
        if (!identity.empty()) { identity["revision"] = 2; second["identity"] = identity; }
        writeMessage(connection, {{"KeyResult", second}});
        if (!identity.empty()) assert(readMessage(connection).at("DisplayAcknowledged").at("senses").empty());
        close(connection);
    });
    char program[] = "qingjian-test"; char disable[] = "--disable=all";
    char *argv[] = {program, disable, nullptr};
    fcitx::Instance instance(2, argv);
    instance.initialize();
    fcitx::QingjianEngine engine(&instance.addonManager());
    auto owned = std::make_unique<Context>(instance.inputContextManager());
    auto &context = *owned;
    context.setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
    context.focusIn();
    assert(engine.process(&context, fcitx::Key(FcitxKey_n)));
    if (reporting && preeditMode == "window") {
        assert(context.inputPanel().clientPreedit().empty());
        assert(context.inputPanel().preedit().toString() == "你'hao");
    } else {
        assert(context.inputPanel().clientPreedit().toString() == "你'hao");
        assert(context.inputPanel().clientPreedit().cursor() == 3);
        assert(context.inputPanel().preedit().empty() == (!reporting || preeditMode != "both"));
    }
    assert(!context.inputPanel().customInputPanelCallback());
    auto candidates = context.inputPanel().candidateList();
    assert(candidates && candidates->size() == 3 && candidates->toPageable()->hasNext());
    assert(candidates->candidate(2).comment().toString() == "hello · 生");
    assert(candidates->candidate(0).isPlaceHolder());
    candidates->candidate(0).select(&context);
    assert(context.committed.empty());
    if (idleDisconnect) {
        mock.join();
        auto timer = instance.eventLoop().addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 50000, 0,
            [&](fcitx::EventSourceTime *, uint64_t) { instance.eventLoop().exit(); return false; });
        instance.eventLoop().exec();
        // 无 addon 测试实例的延迟状态通知会写 auxUp="(Not available)"；
        // 这里核对属于青简的拼音、候选和自绘回调确已清除。
        assert(context.inputPanel().preedit().empty() && context.inputPanel().clientPreedit().empty() &&
               !context.inputPanel().candidateList() && !context.inputPanel().customInputPanelCallback() &&
               context.inputPanel().auxDown().empty());
        candidates->candidate(2).select(&context);
        assert(context.committed.empty());
        owned.reset();
        candidates->toPageable()->next();
        close(listener);
        std::filesystem::remove_all(directory);
        return 0;
    }
    candidates->candidate(2).select(&context);
    assert(context.committed == "你好");
    candidates->candidate(2).select(&context);
    assert(context.committed == "你好"); // 旧帧不能再次上屏。
    assert(context.inputPanel().empty());
    mock.join();
    assert(!engine.process(&context, fcitx::Key(FcitxKey_a)));
    assert(context.inputPanel().empty());
    context.setCapabilityFlags(fcitx::CapabilityFlag::Password);
    assert(!engine.process(&context, fcitx::Key(FcitxKey_n)));
    owned.reset();
    // UI 可以暂存旧列表；上下文销毁后翻页和点选必须安全丢弃。
    candidates->toPageable()->next();
    candidates->candidate(2).select(nullptr);
    Context replacement(instance.inputContextManager());
    candidates->candidate(2).select(&replacement);
    assert(replacement.committed.empty());
    close(listener);
    std::filesystem::remove_all(directory);
}
