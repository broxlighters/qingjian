//! 损坏行内快照不透传已消费按键；两份显示一起清理，有效提交不重放。
#include "qingjian.h"
#include "support/input.h"
#include "support/wire.h"
#include <fcitx/inputcontextmanager.h>
#include <cstring>
#include <filesystem>
#include <thread>
#include <sys/un.h>
#include <unistd.h>

int main(int argc, char **argv) {
    assert(argc == 2);
    const std::string defect = argv[1];
    char directory[] = "/tmp/qingjian-preedit-XXXXXX";
    assert(mkdtemp(directory));
    const std::string path = std::string(directory) + "/server.sock";
    setenv("QINGJIAN_SOCKET", path.c_str(), 1);
    setenv("XDG_CONFIG_HOME", directory, 1);
    setenv("XDG_DATA_HOME", directory, 1);
    int listener = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strcpy(address.sun_path, path.c_str());
    assert(bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    assert(listen(listener, 1) == 0);
    std::thread mock([&] {
        int fd = accept(listener, nullptr, nullptr);
        const auto id = readMessage(fd).at("OpenSession").at("session");
        const Json empty = {{"text", ""}, {"cursor_bytes", 0}};
        writeMessage(fd, {{"Update", {{"session", id}, {"client_preedit", empty}, {"linux_ui", {{"version", 4}}}}}});
        auto identity = readMessage(fd).at("LinuxHello");
        assert(identity.at("version") == 4);
        identity.erase("version"); identity.erase("session"); identity["revision"] = 0;
        writeMessage(fd, {{"LinuxHello", {{"session", id}, {"version", 4}, {"preedit", "both"}}}});
        for (const auto *name : {"Capabilities", "Focus", "Key", "Key"}) {
            auto event = readMessage(fd).at("LinuxEvent").at("event");
            assert(event.contains(name));
            const auto revision = identity.at("revision").get<uint64_t>() + 1;
            identity["revision"] = revision;
            auto frame = emptyFrame();
            Json preedit = empty, commit = nullptr;
            if (revision >= 3) {
                frame["preedit"] = Json::array({{{"text", "ni'hao"}, {"kind", "Typed"}}});
                frame["cursor"] = 6;
                preedit = {{"text", "nihao"}, {"cursor_bytes", 5}};
            }
            if (revision == 4) {
                commit = "你好";
                if (defect == "null") preedit = nullptr;
                if (defect == "text") preedit["text"] = 9;
                if (defect == "negative") preedit["cursor_bytes"] = -1;
                if (defect == "float") preedit["cursor_bytes"] = 1.0;
                if (defect == "beyond") preedit["cursor_bytes"] = 6;
                if (defect == "overflow") preedit["cursor_bytes"] = UINT64_MAX;
                if (defect == "boundary") preedit = {{"text", "ㄋㄧ"}, {"cursor_bytes", 1}};
            }
            Json result = {{"session", id}, {"identity", identity}, {"frame", frame},
                {"client_preedit", preedit}, {"commit", commit}, {"outcome", revision >= 3 ? "Consumed" : "Passthrough"}};
            if (revision == 4 && defect == "missing") result.erase("client_preedit");
            writeMessage(fd, {{"KeyResult", result}});
            if (revision == 3) assert(readMessage(fd).contains("DisplayAcknowledged"));
        }
        char byte;
        assert(recv(fd, &byte, 1, 0) == 0);
        close(fd);
    });
    char program[] = "qingjian-preedit", disable[] = "--disable=all";
    char *arguments[] = {program, disable, nullptr};
    {
        fcitx::Instance instance(2, arguments);
        instance.initialize();
        fcitx::QingjianEngine engine(&instance.addonManager());
        Input context(instance.inputContextManager());
        context.focusIn();
        assert(engine.process(&context, fcitx::Key(FcitxKey_n)));
        assert(context.preedit() == "nihao" && context.inputPanel().preedit().toString() == "ni'hao");
        assert(engine.process(&context, fcitx::Key(FcitxKey_space)));
        assert(context.inputPanel().empty());
        assert(context.committed == "你好" && context.commits == 1);
        mock.join();
        fcitx::InputMethodEntry entry("qingjian", "qingjian", "zh_CN", "qingjian");
        context.focusOut();
        fcitx::FocusOutEvent out(&context);
        engine.deactivate(entry, out);
        assert(context.committed == "你好" && context.commits == 1);
    }
    close(listener);
    std::filesystem::remove_all(directory);
}
