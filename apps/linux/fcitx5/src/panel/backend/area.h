//! 选择光标所在的单个物理屏幕，再与桌面保留区相交；屏幕间隙不能作为显示区域。
#pragma once
#include <fcitx-utils/rect.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>
namespace qingjian::panel {
inline fcitx::Rect availableArea(const std::vector<fcitx::Rect> &monitors,
                                  const std::optional<fcitx::Rect> &work,
                                  const fcitx::Rect &cursor) {
    fcitx::Rect selected;
    int64_t nearest = std::numeric_limits<int64_t>::max();
    const int x = cursor.left(), y = cursor.top();
    for (const auto &monitor : monitors) {
        if (monitor.width() <= 0 || monitor.height() <= 0) continue;
        const int64_t dx = int64_t(x) - std::clamp(x, monitor.left(), monitor.right() - 1);
        const int64_t dy = int64_t(y) - std::clamp(y, monitor.top(), monitor.bottom() - 1);
        const auto distance = dx * dx + dy * dy;
        if (distance < nearest) { selected = monitor; nearest = distance; }
    }
    if (!work || nearest == std::numeric_limits<int64_t>::max()) return selected;
    const int left = std::max(selected.left(), work->left());
    const int top = std::max(selected.top(), work->top());
    const int right = std::min(selected.right(), work->right());
    const int bottom = std::min(selected.bottom(), work->bottom());
    return left < right && top < bottom ? fcitx::Rect(left, top, right, bottom) : fcitx::Rect();
}
}
