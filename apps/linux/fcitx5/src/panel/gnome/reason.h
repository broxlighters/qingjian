//! 只接受协议定义的原因码；远端任意错误文本不进入日志。
#pragma once
#include <array>
#include <string>
#include <string_view>
namespace qingjian::panel {
inline std::string failureReason(std::string_view value, std::string_view fallback = "transport_lost") {
    constexpr std::string_view prefix = "org.qingjian.Panel1.";
    if (value.starts_with(prefix)) value.remove_prefix(prefix.size());
    constexpr std::array codes = {"extension_missing", "protocol_mismatch", "unsupported_frontend", "anchor_unavailable",
        "focus_mismatch", "scale_unresolved", "buffer_invalid", "paint_timeout", "transport_lost", "renderer_unavailable", "xcb_failure"};
    for (const auto *code : codes) if (value == code) return std::string(value);
    return std::string(fallback);
}
}
