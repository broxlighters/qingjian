//! 初始化真实 Fcitx 事件链，覆盖客户端/服务端 preedit 与提交前隐私刷新。
#include "qingjian.h"
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputpanel.h>
#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <thread>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
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
int main(int argc, char **argv) {
    assert(argc == 2);
    const std::string scenario = argv[1];
    char directory[] = "/tmp/qingjian-lifecycle-XXXXXX";
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
    std::vector<Json> messages;
    std::thread mock([&] {
        int connection = accept(listener, nullptr, nullptr);
        messages.push_back(readMessage(connection));
        assert(messages.back().contains("OpenSession"));
        writeMessage(connection, {{"Update", {{"session", 1}, {"frame", Json::object()}}}});
        bool privateInput = true;
        std::string composition;
        char byte;
        while (recv(connection, &byte, 1, MSG_PEEK) > 0) {
            auto message = readMessage(connection);
            messages.push_back(message);
            if (message.contains("Privacy")) {
                bool next = message["Privacy"]["private"];
                if (privateInput != next) composition.clear();
                privateInput = next;
            } else if (message.contains("Key")) {
                composition = "ni";
                Json frame = Json::parse(R"({"preedit":[{"text":"ni","kind":"Typed"}],"cursor":2,"candidates":{"items":[]},"highlight":0,"page":0,"page_count":1,"notice":null})");
                writeMessage(connection, {{"KeyResult", {{"session", 1}, {"outcome", "Consumed"}, {"commit", nullptr}, {"frame", frame}}}});
            } else if (message.contains("Commit")) {
                Json text = composition.empty() ? Json(nullptr) : Json(composition);
                writeMessage(connection, {{"Committed", {{"session", 1}, {"text", text}}}});
                composition.clear();
            } else { assert(false); }
        }
        close(connection);
    });
    {
    char program[] = "qingjian-test"; char disable[] = "--disable=all";
    char *arguments[] = {program, disable, nullptr};
    fcitx::Instance instance(2, arguments);
    instance.initialize();
    fcitx::QingjianEngine engine(&instance.addonManager());
    fcitx::InputMethodEntry entry("qingjian", "qingjian", "zh_CN", "qingjian");
    Context context(instance.inputContextManager());
    using Flag = fcitx::CapabilityFlag;
    fcitx::CapabilityFlags capabilities = Flag::Preedit;
    if (scenario == "focusout-panel") capabilities = Flag::SurroundingText;
    if (scenario == "focusout-client-owned") capabilities |= Flag::ClientUnfocusCommit;
    context.setCapabilityFlags(capabilities);
    context.focusIn();
    assert(engine.process(&context, fcitx::Key(FcitxKey_n)));
    if (scenario.rfind("focusout-", 0) == 0) {
        if (scenario == "focusout-client-owned") {
            // 模拟声明 ClientUnfocusCommit 的客户端执行自己的失焦提交。
            context.commitString(context.inputPanel().clientPreedit().toStringForCommit());
        }
        context.focusOut(); // 真正经过 ReservedFirst：框架只提交 clientPreedit。
        fcitx::FocusOutEvent event(&context);
        engine.deactivate(entry, event);
        assert(context.committed == "ni");
    } else {
        bool password = scenario.find("password") != std::string::npos;
        bool sensitive = scenario.find("sensitive") != std::string::npos;
        bool disabled = scenario.find("disabled") != std::string::npos;
        if (scenario == "capability-deactivate") {
            fcitx::InputContextSwitchInputMethodEvent event(fcitx::InputMethodSwitchedReason::CapabilityChanged, "qingjian", &context);
            engine.deactivate(entry, event); // 此时 capability 尚是旧值。
        } else {
            context.setCapabilityFlags(password ? fcitx::CapabilityFlags(Flag::Password) :
                sensitive ? fcitx::CapabilityFlags(Flag::Sensitive) :
                disabled ? fcitx::CapabilityFlags(Flag::Disable) : fcitx::CapabilityFlags());
            assert(context.inputPanel().empty()); // 能力变化必须立即同步并清面板。
            if (scenario.rfind("shift-", 0) == 0) {
                fcitx::KeyEvent press(&context, fcitx::Key(FcitxKey_Shift_L), false);
                fcitx::KeyEvent release(&context, fcitx::Key(FcitxKey_Shift_L), true);
                engine.keyEvent(entry, press);
                engine.keyEvent(entry, release);
            } else {
                fcitx::InputContextEvent event(&context, fcitx::EventType::InputContextInputMethodDeactivated);
                engine.deactivate(entry, event);
            }
        }
        assert(context.committed.empty());
        fcitx::InputContextEvent reset(&context, fcitx::EventType::InputContextReset);
        engine.reset(entry, reset);
    }
    assert(context.inputPanel().empty());
    mock.join();
    bool privateBeforeCommit = false;
    bool sawPrivate = false;
    size_t commits = 0;
    for (const auto &message : messages) {
        if (message.contains("Privacy")) {
            privateBeforeCommit = message["Privacy"]["private"].get<bool>();
            sawPrivate |= privateBeforeCommit;
        }
        if (message.contains("Commit")) {
            ++commits;
            if (scenario.rfind("focusout-", 0) != 0) assert(privateBeforeCommit);
        }
    }
    if (scenario.rfind("focusout-", 0) == 0) assert(commits == 1);
    else {
        assert(sawPrivate);
        if (scenario.find("password") != std::string::npos || scenario.find("disabled") != std::string::npos || scenario == "shift-sensitive" || scenario == "capability-deactivate") assert(commits == 0);
        else assert(commits == 1);
    }
    }
    close(listener);
    std::filesystem::remove_all(directory);
}
