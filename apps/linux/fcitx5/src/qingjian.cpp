//! Fcitx5 插件：IPC 失败清面板并放行，所有学习与选词在 Server。
#include "qingjian.h"
#include "candidate/list.h"
#include "candidate/word.h"
#include "key/mapping.h"
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/text.h>
#include <fcitx-utils/log.h>

namespace fcitx {
QingjianEngine::QingjianEngine(AddonManager *manager)
    : sessions_([](InputContext &) { return new qingjian::Session; }) {
    manager->instance()->inputContextManager().registerProperty("qingjian-session", &sessions_);
    capabilityWatcher_ = manager->instance()->watchEvent(EventType::InputContextCapabilityChanged, EventWatcherPhase::PreInputMethod, [this](Event &event) {
        auto *context = static_cast<InputContextEvent &>(event).inputContext();
        if (context->propertyFor(&sessions_)->opened) syncPrivacy(context);
    });
}
void QingjianEngine::clear(InputContext *context) {
    context->inputPanel().reset();
    context->updatePreedit();
    context->updateUserInterface(UserInterfaceComponent::InputPanel);
}
void QingjianEngine::disconnect(InputContext *context) {
    auto *session = context->propertyFor(&sessions_);
    session->connection.close();
    session->opened = false;
    session->privateInput = true;
    session->clientPreedit = false;
    session->shiftPending = false;
    ++session->revision;
    clear(context);
}
bool QingjianEngine::syncPrivacy(InputContext *context) {
    auto *session = context->propertyFor(&sessions_);
    const auto caps = context->capabilityFlags();
    bool disabled = caps.test(CapabilityFlag::Password) || caps.test(CapabilityFlag::Disable);
    bool privateInput = disabled || caps.test(CapabilityFlag::Sensitive) || caps == CapabilityFlags();
    if (session->opened) {
        if (!session->connection.send({{"Privacy", {{"session", session->id}, {"private", privateInput}}}})) {
            disconnect(context);
            return false;
        }
        if (session->privateInput != privateInput) {
            // Server 在 Privacy 变化时丢弃输入状态；面板必须同时清理。
            ++session->revision;
            clear(context);
        }
        session->privateInput = privateInput;
    }
    if (disabled) { disconnect(context); return false; }
    return true;
}
bool QingjianEngine::commitRaw(InputContext *context, bool deliver) {
    auto *session = context->propertyFor(&sessions_);
    if (!syncPrivacy(context)) return false;
    if (!session->opened) return true;
    try {
        nlohmann::json response;
        if (!session->connection.send({{"Commit", {{"session", session->id}}}}, &response)) throw std::runtime_error("commit exchange");
        auto &result = response.at("Committed");
        if (result.at("session") != session->id) throw std::runtime_error("session mismatch");
        const auto &text = result.at("text");
        if (!text.is_null() && !text.is_string()) throw std::runtime_error("bad commit");
        ++session->revision;
        clear(context);
        if (deliver && text.is_string()) context->commitString(text.get<std::string>());
        return true;
    } catch (const std::exception &) {
        disconnect(context);
        return false;
    }
}
void QingjianEngine::reset(const InputMethodEntry &, InputContextEvent &event) {
    auto *context = event.inputContext();
    syncPrivacy(context);
    disconnect(context);
}
void QingjianEngine::deactivate(const InputMethodEntry &entry, InputContextEvent &event) {
    auto *context = event.inputContext();
    auto *session = context->propertyFor(&sessions_);
    // Fcitx 在 capability 即将改变时就会停用输入法，此时 context 仍返回旧能力。
    // 该事件必须保守清理，不能把旧组句提交到新密码框。
    auto *switched = dynamic_cast<InputContextSwitchInputMethodEvent *>(&event);
    if (switched && switched->reason() == InputMethodSwitchedReason::CapabilityChanged) {
        if (session->opened) session->connection.send({{"Privacy", {{"session", session->id}, {"private", true}}}});
        disconnect(context);
        return;
    }
    // FocusOut 的 clientPreedit 已由 Fcitx（或 ClientUnfocusCommit 客户端）提交。
    // 只有 panel.preedit 的上下文没有这层兜底，需交付 Server 的原样组句。
    // 按最近一帧的去向判断，避免框架处理面板后的状态影响提交判定。
    bool frameworkCommit = event.type() == EventType::InputContextFocusOut && session->clientPreedit;
    commitRaw(context, !frameworkCommit);
    reset(entry, event);
}
void QingjianEngine::keyEvent(const InputMethodEntry &, KeyEvent &event) {
    auto *context = event.inputContext();
    if (!context) return;
    auto *session = context->propertyFor(&sessions_);
    bool shift = event.rawKey().sym() == FcitxKey_Shift_L || event.rawKey().sym() == FcitxKey_Shift_R;
    if (event.isRelease()) {
        if (shift && session->shiftPending) {
            session->shiftPending = false;
            if (!syncPrivacy(context)) return;
            if (!context->capabilityFlags().testAny(CapabilityFlag::PasswordOrSensitive)) {
                // 释放事件不作为 Key 发给 Core；所有提交统一经过隐私刷新。
                if (!commitRaw(context, true)) return;
                session->english = !session->english;
                event.filterAndAccept();
            }
        }
        return;
    }
    session->shiftPending = shift && !event.rawKey().states().test(KeyState::Ctrl) && !event.rawKey().states().test(KeyState::Alt) && !event.rawKey().states().test(KeyState::Super);
    if (process(context, event.rawKey())) event.filterAndAccept();
}
bool QingjianEngine::process(InputContext *context, const Key &key) {
    auto *session = context->propertyFor(&sessions_);
    if (key.isModifier()) { syncPrivacy(context); return false; }
    if (context->capabilityFlags().test(CapabilityFlag::Password) || context->capabilityFlags().test(CapabilityFlag::Disable)) { syncPrivacy(context); return false; }
    try {
        if (!session->opened) {
            nlohmann::json response;
            if (!session->connection.open() || !session->connection.send({{"OpenSession", {{"session", session->id}, {"app", context->program()}, {"protocol", 4}}}}, &response)
                || !response.contains("Update") || response.at("Update").at("session") != session->id) {
                session->connection.close(); clear(context); return false;
            }
            session->opened = true;
            session->privateInput = true;
        }
        if (!syncPrivacy(context)) return false;
        nlohmann::json response;
        if (!session->connection.send({{"Key", {{"session", session->id}, {"event", qingjian::mapKey(key, session->english)}}}}, &response)) throw std::runtime_error("key exchange");
        auto &result = response.at("KeyResult");
        if (result.at("session") != session->id) throw std::runtime_error("session mismatch");
        const std::string outcome = result.at("outcome");
        if (outcome != "Consumed" && outcome != "Passthrough") throw std::runtime_error("bad outcome");
        render(context, result.at("frame"));
        if (result.at("commit").is_string()) context->commitString(result.at("commit").get<std::string>());
        return outcome == "Consumed";
    } catch (const std::exception &) {
        disconnect(context); return false;
    }
}
void QingjianEngine::render(InputContext *context, const nlohmann::json &frame) {
    auto *session = context->propertyFor(&sessions_);
    auto revision = ++session->revision;
    Text preedit;
    for (const auto &segment : frame.at("preedit")) preedit.append(segment.at("text").get<std::string>(), TextFormatFlag::Underline);
    auto raw = preedit.toString();
    size_t cursor = frame.at("cursor").get<size_t>();
    size_t bytes = 0;
    for (size_t chars = 0; bytes < raw.size() && chars < cursor; ++bytes) if ((static_cast<unsigned char>(raw[bytes]) & 0xc0) != 0x80) ++chars;
    while (bytes < raw.size() && (static_cast<unsigned char>(raw[bytes]) & 0xc0) == 0x80) ++bytes;
    preedit.setCursor(static_cast<int>(bytes));
    auto &panel = context->inputPanel();
    panel.reset();
    if (context->capabilityFlags().test(CapabilityFlag::Preedit)) panel.setClientPreedit(preedit);
    else panel.setPreedit(preedit);
    session->clientPreedit = context->capabilityFlags().test(CapabilityFlag::Preedit);
    const auto &items = frame.at("candidates").at("items");
    if (!items.empty()) {
        auto list = std::make_unique<qingjian::List>(frame.at("page").get<int>(), frame.at("page_count").get<int>(), [this, context, session, revision](bool next) {
            if (session->revision == revision) process(context, Key(next ? FcitxKey_Page_Down : FcitxKey_Page_Up));
        });
        list->setPageSize(9);
        list->setLabels({"1", "2", "3", "4", "5", "6", "7", "8", "9"});
        list->setLayoutHint(CandidateLayoutHint::Horizontal);
        size_t index = 0;
        for (const auto &item : items) {
            std::string annotation;
            const auto &translation = item.at("translation");
            if (translation.is_object() && !translation.at("senses").empty()) {
                const auto &sense = translation.at("senses").front();
                annotation = sense.at("text").get<std::string>();
                if (sense.value("fresh", false)) annotation += " · 生";
            }
            list->append(std::make_unique<qingjian::Word>(item.at("text").get<std::string>(), annotation, [this, index, revision](InputContext *ic) {
                auto *current = ic->propertyFor(&sessions_);
                if (current->revision == revision) process(ic, Key(static_cast<KeySym>(FcitxKey_1 + index)));
            }));
            ++index;
        }
        auto highlight = frame.at("highlight").get<size_t>();
        if (highlight < items.size()) list->setGlobalCursorIndex(static_cast<int>(highlight));
        panel.setCandidateList(std::move(list));
    }
    if (frame.contains("notice") && frame["notice"].is_string()) panel.setAuxDown(Text(frame["notice"].get<std::string>()));
    context->updatePreedit();
    context->updateUserInterface(UserInterfaceComponent::InputPanel);
}
}
