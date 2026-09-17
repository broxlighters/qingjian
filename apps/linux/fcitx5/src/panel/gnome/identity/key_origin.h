//! 只在真实 dbusfrontend 按键调用栈内复制来源；它不证明 Shell 窗口身份。
#pragma once
#include <fcitx-utils/dbus/objectvtable.h>
#include <fcitx-utils/dbus/message.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

namespace qingjian::panel {
struct KeyOrigin final {
    std::string sender;

    std::string path;

    std::string context;

    std::string method;

    uint32_t time = 0;

    uint32_t code = 0;

    bool release = false;

    static std::optional<KeyOrigin> capture(const fcitx::KeyEvent &event) {
        auto *context = event.inputContext();
        if (!context || !context->hasFocus() || context->frontendName() != "dbus") return {};
        auto *object = dynamic_cast<fcitx::dbus::ObjectVTableBase *>(context);
        constexpr const char *interface = "org.fcitx.Fcitx.InputContext1";
        if (!object || !object->isRegistered() || object->interface() != interface) return {};
        const auto *message = object->currentMessage();
        if (!message || message->type() != fcitx::dbus::MessageType::MethodCall ||
            message->interface() != interface || message->path() != object->path() ||
            message->signature() != "uuubu") return {};
        const auto member = message->member();
        if (member != "ProcessKeyEvent" && member != "ProcessKeyEventBatch") return {};
        const auto sender = message->sender();
        if (sender.size() < 2 || sender.front() != ':') return {};
        std::ostringstream uuid;
        uuid << std::hex << std::setfill('0');
        for (auto byte : context->uuid()) uuid << std::setw(2) << static_cast<unsigned>(byte);
        // currentMessage 在框架返回方法时清空；不保存裸指针、不读取/重绕消息体。
        // dbusfrontend 在 CHECK_SENDER_OR_RETURN 之后同步调用此 KeyEvent。
        return KeyOrigin{sender, message->path(), uuid.str(), member,
            static_cast<uint32_t>(event.time()), static_cast<uint32_t>(event.rawKey().code()), event.isRelease()};
    }
};
}
