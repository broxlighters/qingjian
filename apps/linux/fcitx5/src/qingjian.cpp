//! Fcitx5 插件：IPC 失败清面板并放行，所有学习与选词在 Server。
#include "qingjian.h"
#include "candidate/list.h"
#include "candidate/word.h"
#include "key/mapping.h"
#include "panel/gnome/diagnostics.h"
#if defined(QJ_GNOME_BACKEND)
#include "panel/gnome/bridge.h"
#endif
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/text.h>
#include <fcitx/userinterfacemanager.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/log.h>
#include <array>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <cerrno>

namespace fcitx {
namespace {
qingjian::panel::RendererMode rendererMode(const std::string &value) {
    if (value == "qingjian") return qingjian::panel::RendererMode::Qingjian;
    if (value == "auto") return qingjian::panel::RendererMode::Auto;
    return qingjian::panel::RendererMode::Fcitx;
}
std::string contextIdentity(const InputContext *context) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto byte : context->uuid()) out << std::setw(2) << static_cast<unsigned>(byte);
    return out.str();
}
}
QingjianEngine::QingjianEngine(AddonManager *manager)
    : instance_(manager->instance()), sessions_([](InputContext &) { return new qingjian::Session; }) {
    qingjian::panel::prepareRenderer();
#if defined(QJ_GNOME_BACKEND)
    gnomeBridge_ = qingjian::panel::GnomeBridge::shared(instance_->eventLoop());
#endif
    manager->instance()->inputContextManager().registerProperty("qingjian-session", &sessions_);
#if defined(QJ_RENDER_FFI)
    appearance_ = std::make_unique<qingjian::panel::Appearance>(instance_->eventLoop(), [this] {
        instance_->inputContextManager().foreach([this](InputContext *context) {
            auto *session = context->propertyFor(&sessions_);
            if (session->opened && context->hasFocus() && session->panel.active() &&
                !session->lastFrame.is_null()) {
                auto frame = session->lastFrame;
                try { render(context, frame); }
                catch (const std::exception &) { clear(context); }
            }
            return true;
        });
    });
#endif
    capabilityWatcher_ = manager->instance()->watchEvent(EventType::InputContextCapabilityChanged, EventWatcherPhase::PreInputMethod, [this](Event &event) {
        auto *context = static_cast<InputContextEvent &>(event).inputContext();
        auto *session = context->propertyFor(&sessions_);
        const bool owned = session->panel.active();
        if (owned && std::getenv("QINGJIAN_UI_DIAGNOSTICS"))
            FCITX_INFO() << "青简能力撤销自绘 callback=" << bool(context->inputPanel().customInputPanelCallback())
                << " opened=" << session->opened << " focus=" << context->hasFocus();
        session->focusIdentity.invalidate();
        if (owned) session->panel.hide(context);
        if (session->opened) syncPrivacy(context);
        // UI 后端重载也会改变 capability；撤销接管后立即重建默认帧，不能只取消
        // 自绘计时器而永远等不到下一次按键。未拥有的其他 IME callback 不碰。
        if (session->opened && context->hasFocus() && !session->lastFrame.is_null()) {
            auto frame = session->lastFrame;
            try { render(context, frame); }
            catch (const std::exception &) { clear(context); }
        } else if (owned) context->updateUserInterface(UserInterfaceComponent::InputPanel);
    });
    cursorWatcher_ = instance_->watchEvent(EventType::InputContextCursorRectChanged, EventWatcherPhase::PostInputMethod, [this](Event &event) {
        auto *context = static_cast<InputContextEvent &>(event).inputContext();
        qingjian::panel::traceContext(context, "cursor");
        auto *session = context->propertyFor(&sessions_);
        if (session->opened && context->hasFocus() && session->panel.active() && !session->lastFrame.is_null()) {
            auto frame = session->lastFrame;
            try { render(context, frame); }
            catch (const std::exception &) { clear(context); }
        }
    });
    for (const auto type : {EventType::InputContextFocusIn, EventType::InputContextFocusOut, EventType::InputContextReset}) {
        diagnosticWatchers_.push_back(instance_->watchEvent(type, EventWatcherPhase::PreInputMethod, [this, type](Event &event) {
            auto *context = static_cast<InputContextEvent &>(event).inputContext();
            auto *session = context->propertyFor(&sessions_);
            session->focusIdentity.invalidate();
            if (session->panel.active()) session->panel.hide(context);
            qingjian::panel::traceContext(context, type == EventType::InputContextReset ? "reset" :
                type == EventType::InputContextFocusIn ? "focus-in" : "focus-out");
        }));
    }
    virtualKeyboardWatcher_ = instance_->watchEvent(EventType::VirtualKeyboardVisibilityChanged, EventWatcherPhase::PostInputMethod, [this](Event &) {
        const bool visible = instance_->userInterfaceManager().isVirtualKeyboardVisible();
        instance_->inputContextManager().foreach([this, visible](InputContext *context) {
            auto *session = context->propertyFor(&sessions_);
            if (!session->opened && !session->panel.active()) return true;
            if (!visible) {
                if (session->opened && context->hasFocus() && !session->lastFrame.is_null()) {
                    auto frame = session->lastFrame;
                    try { render(context, frame); }
                    catch (const std::exception &) { clear(context); }
                }
                return true;
            }
            // 等待准备/更新时active可为false，但旧XCB映射或Shell请求仍需撤销。
            session->panel.hide(context);
            if (session->displayReporting && !session->privateInput) {
                if (!session->connection.send({{"DisplayAcknowledged", {{"session", session->id}, {"identity", session->displayIdentity}, {"senses", nlohmann::json::array()}}}})) disconnect(context);
            }
            context->updateUserInterface(UserInterfaceComponent::InputPanel);
            return true;
        });
    });
}
void QingjianEngine::clear(InputContext *context) {
    auto *session = context->propertyFor(&sessions_);
    session->panel.invalidate(context);
    session->lastFrame = nullptr;
    context->inputPanel().reset();
    context->updatePreedit();
    context->updateUserInterface(UserInterfaceComponent::InputPanel);
}
void QingjianEngine::disconnect(InputContext *context) {
    auto *session = context->propertyFor(&sessions_);
    session->socketWatcher.reset();
    session->connection.close();
    session->opened = false;
    session->displayReporting = false;
    session->displayIdentity = nullptr;
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
    session->panel.hide(context);
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
    qingjian::panel::traceKeyOrigin(event);
    auto *session = context->propertyFor(&sessions_);
    session->focusIdentity.observe(event);
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
            if (!session->connection.open()) {
                session->connection.close(); clear(context); return false;
            }
            ++session->generation;
            if (!session->connection.send({{"OpenSession", {{"session", session->id}, {"app", context->program()}, {"protocol", 4}}}}, &response)
                || !response.contains("Update") || response.at("Update").at("session") != session->id) {
                session->connection.close(); clear(context); return false;
            }
            session->opened = true;
            session->privateInput = true;
            session->preeditMode = "legacy";
            session->panel.configure(qingjian::panel::RendererMode::Fcitx, &instance_->eventLoop());
            const auto &update = response.at("Update");
            // 配置和曝光协商不依赖窗口能力；默认 Fcitx 也需要 preedit 配置。
            if (update.contains("linux_ui") && update.at("linux_ui").value("version", 0) == 1) {
                nlohmann::json helloResponse;
                if (!session->connection.send({{"LinuxHello", {{"version", 1}, {"generation", session->generation}, {"context", contextIdentity(context)}}}}, &helloResponse)
                    || helloResponse.at("LinuxHello").value("version", 0) != 1) throw std::runtime_error("linux ui handshake");
                const auto &settings = helloResponse.at("LinuxHello");
                session->displayReporting = true;
                session->preeditMode = settings.value("preedit", "both");
                session->panel.configure(rendererMode(settings.value("renderer", "fcitx")), &instance_->eventLoop(),
                    qingjian::panel::SizeOptions::fromSettings(settings));
            }
            session->socketWatcher = instance_->eventLoop().addIOEvent(session->connection.fd(), IOEventFlag::In,
                [this, context](EventSourceIO *, int fd, IOEventFlags) {
                    char byte;
                    auto count = recv(fd, &byte, 1, MSG_PEEK | MSG_DONTWAIT);
                    if (count >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) disconnect(context);
                    return true;
                });
        }
        if (!syncPrivacy(context)) return false;
        nlohmann::json response;
        if (!session->connection.send({{"Key", {{"session", session->id}, {"event", qingjian::mapKey(key, session->english)}}}}, &response)) throw std::runtime_error("key exchange");
        auto &result = response.at("KeyResult");
        if (result.at("session") != session->id) throw std::runtime_error("session mismatch");
        const std::string outcome = result.at("outcome");
        if (outcome != "Consumed" && outcome != "Passthrough") throw std::runtime_error("bad outcome");
        if (session->displayReporting) {
            const auto &identity = result.at("identity");
            if (identity.at("generation") != session->generation || identity.at("context") != contextIdentity(context)
                || !identity.at("revision").is_number_unsigned()) throw std::runtime_error("display identity");
            session->displayIdentity = identity;
        }
        // 展示故障不改变 Server 已接受的按键或上屏结果。
        try { render(context, result.at("frame")); }
        catch (const std::exception &) { clear(context); }
        if (result.at("commit").is_string()) context->commitString(result.at("commit").get<std::string>());
        return outcome == "Consumed";
    } catch (const std::exception &) {
        disconnect(context); return false;
    }
}
void QingjianEngine::render(InputContext *context, const nlohmann::json &frame) {
    qingjian::panel::traceContext(context, "frame");
    const auto ready = std::chrono::steady_clock::now();
    auto *session = context->propertyFor(&sessions_);
    auto revision = ++session->revision;
    const auto focusEpoch = session->focusIdentity.epoch;
    Text preedit;
    for (const auto &segment : frame.at("preedit")) preedit.append(segment.at("text").get<std::string>(), TextFormatFlag::Underline);
    auto raw = preedit.toString();
    size_t cursor = frame.at("cursor").get<size_t>();
    size_t bytes = 0;
    for (size_t chars = 0; bytes < raw.size() && chars < cursor; ++bytes) if ((static_cast<unsigned char>(raw[bytes]) & 0xc0) != 0x80) ++chars;
    while (bytes < raw.size() && (static_cast<unsigned char>(raw[bytes]) & 0xc0) == 0x80) ++bytes;
    preedit.setCursor(static_cast<int>(bytes));
    auto &panel = context->inputPanel();
    const bool updatingCustom = session->panel.canUpdate(context);
    if (!updatingCustom) session->panel.hide(context);
    panel.reset();
    // 先对原 UI 提交空帧并立即 flush，再安装自定义回调，避免默认窗口残留。
    if (!updatingCustom && session->panel.eligible(context)) {
        context->updateUserInterface(UserInterfaceComponent::InputPanel);
        instance_->userInterfaceManager().flush();
    }
    const bool inlinePreedit = context->capabilityFlags().test(CapabilityFlag::Preedit) && session->preeditMode != "window";
    const bool windowPreedit = session->preeditMode == "both" || session->preeditMode == "window" || !inlinePreedit;
    if (inlinePreedit) panel.setClientPreedit(preedit);
    if (windowPreedit) panel.setPreedit(preedit);
    session->clientPreedit = inlinePreedit;
    session->lastFrame = session->privateInput ? nlohmann::json(nullptr) : frame;
    const auto &items = frame.at("candidates").at("items");
    if (!items.empty()) {
        auto watched = context->watch();
        auto list = std::make_unique<qingjian::List>(frame.at("page").get<int>(), frame.at("page_count").get<int>(), [this, watched, revision](bool next) {
            auto *ic = watched.get();
            if (!ic) return;
            auto *current = ic->propertyFor(&sessions_);
            if (current->opened && current->revision == revision && ic->hasFocus()) process(ic, Key(next ? FcitxKey_Page_Down : FcitxKey_Page_Up));
        });
        list->setPageSize(9);
        list->setLabels({"1", "2", "3", "4", "5", "6", "7", "8", "9"});
        list->setLayoutHint(frame.value("layout", "horizontal") == "vertical" ? CandidateLayoutHint::Vertical : CandidateLayoutHint::Horizontal);
        size_t index = 0;
        for (const auto &item : items) {
            std::string annotation;
            const auto &translation = item.at("translation");
            if (translation.is_object() && !translation.at("senses").empty()) {
                const auto &sense = translation.at("senses").front();
                annotation = sense.at("text").get<std::string>();
                if (sense.value("fresh", false)) annotation += " · 生";
            }
            list->append(std::make_unique<qingjian::Word>(item.at("text").get<std::string>(), annotation, [this, watched, index, revision](InputContext *ic) {
                if (!ic || ic != watched.get()) return;
                auto *current = ic->propertyFor(&sessions_);
                if (current->opened && current->revision == revision && ic->hasFocus()) process(ic, Key(static_cast<KeySym>(FcitxKey_1 + index)));
            }));
            ++index;
        }
        auto highlight = frame.at("highlight").get<size_t>();
        if (highlight < items.size()) list->setGlobalCursorIndex(static_cast<int>(highlight));
        panel.setCandidateList(std::move(list));
    }
    if (frame.contains("notice") && frame["notice"].is_string()) panel.setAuxDown(Text(frame["notice"].get<std::string>()));
    bool custom = false;
    if (session->displayReporting && session->displayIdentity.is_object()) {
        const auto identity = session->displayIdentity;
        auto watched = context->watch();
        auto acknowledge = [this, watched, revision, identity](const nlohmann::json &senses) {
            auto *context = watched.get();
            if (!context) return;
            auto *session = context->propertyFor(&sessions_);
            if (!session->opened || session->revision != revision || !context->hasFocus() || session->privateInput) return;
            // 同一帧的缩放/回退可能改变可见义项；Server 替换集合，上屏时才计数。
            if (!session->connection.send({{"DisplayAcknowledged", {{"session", session->id}, {"identity", identity}, {"senses", senses}}}})) disconnect(context);
        };
        auto defaultSenses = nlohmann::json::array();
        for (size_t row = 0; row < items.size(); ++row) {
            const auto &item = items.at(row);
            if (!item.at("text").get<std::string>().empty() && item.at("translation").is_object()
                && !item.at("translation").at("senses").empty()) defaultSenses.push_back({row, 0});
        }
        auto displayFrame = frame;
        if (!windowPreedit) { displayFrame["preedit"] = nlohmann::json::array(); displayFrame["cursor"] = 0; }
        if (instance_->userInterfaceManager().isVirtualKeyboardVisible()) {
            session->panel.hide(context);
            // 由框架决定屏幕键盘显示，无法确认其可见义项范围。
            acknowledge(nlohmann::json::array());
            custom = true;
        } else custom = session->panel.renderFrame(context, displayFrame,
            {identity.at("generation"), identity.at("context"), identity.at("revision")},
            [this, watched, revision, focusEpoch] {
                auto *context = watched.get();
                if (!context) return false;
                auto *session = context->propertyFor(&sessions_);
                return session->opened && session->revision == revision && session->focusIdentity.epoch == focusEpoch && context->hasFocus() && !session->privateInput &&
                    !instance_->userInterfaceManager().isVirtualKeyboardVisible();
            },
            [this, watched, revision](int value) {
                auto *context = watched.get();
                if (!context) return;
                auto *session = context->propertyFor(&sessions_);
                if (!session->opened || session->revision != revision || !context->hasFocus()) return;
                process(context, Key(value == -1 ? FcitxKey_Page_Up : value == -2 ? FcitxKey_Page_Down : static_cast<KeySym>(FcitxKey_1 + value)));
            }, acknowledge,
            [this, watched, revision, acknowledge, defaultSenses] {
                auto *context = watched.get();
                if (!context) return;
                auto *session = context->propertyFor(&sessions_);
                if (!session->opened || session->revision != revision || !context->hasFocus()) return;
                context->updateUserInterface(UserInterfaceComponent::InputPanel);
                acknowledge(defaultSenses);
            }, ready, appearance_ && appearance_->dark(), appearance_ ? appearance_->textScale() : 1.0,
                appearance_ && appearance_->textScaleKnown(), session->focusIdentity,
                [this, watched, revision] {
                    auto *context = watched.get();
                    if (!context) return;
                    auto *session = context->propertyFor(&sessions_);
                    if (session->opened && session->revision == revision && context->hasFocus() &&
                        !session->privateInput && session->lastFrame.is_object()) {
                        const auto frame = session->lastFrame;
                        render(context, frame);
                    }
                });
        if (!custom) acknowledge(defaultSenses);
    }
    context->updatePreedit();
    context->updateUserInterface(UserInterfaceComponent::InputPanel);
}
}
