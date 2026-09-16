//! 跨连接和上下文的展示身份，不能只使用连接内 session=1。
#pragma once
#include <cstdint>
#include <string>
namespace qingjian::panel {
struct FrameIdentity {
    /// 每次成功连接递增。
    uint64_t generation = 0;

    /// Fcitx 输入上下文 UUID。
    std::string context;

    /// 服务端生成的帧版本。
    uint64_t revision = 0;

    bool operator==(const FrameIdentity &) const = default;
};
}
