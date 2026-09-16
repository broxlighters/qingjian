//! 窗口承载边界；输入坐标和大小均为物理像素。
#pragma once
#include "timing.h"
#include <fcitx-utils/rect.h>
#include <cstdint>
#include <functional>
namespace qingjian::panel {
class Backend {
public:
    virtual ~Backend() = default;
    virtual fcitx::Rect bounds(const fcitx::Rect &cursor) = 0;
    virtual bool present(const uint8_t *rgba, uint32_t width, uint32_t height,
                         uint32_t stride, const fcitx::Rect &cursor) = 0;
    virtual void hide() = 0;
    virtual void drain() = 0;
    // 回调返回 false 表示已选词或翻页，丢弃同批旧帧事件。
    virtual bool poll(const std::function<bool(int, int, unsigned)> &click) = 0;
    virtual int fd() const = 0;
    /// 合成器或连接失效时回退；默认用于无需外部协议的测试承载。
    virtual bool healthy() { return true; }
    virtual SubmissionTiming timing() const { return {}; }
};
}
