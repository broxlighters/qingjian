//! 光标与可用屏幕同属物理坐标，不再次乘客户端缩放。
#pragma once
#include <fcitx-utils/rect.h>
#include <algorithm>
#include <utility>
namespace qingjian::panel {
inline std::pair<int, int> place(const fcitx::Rect &screen,
                                const fcitx::Rect &cursor, int width, int height) {
    const int x = std::clamp(cursor.left(), screen.left(),
                             std::max(screen.left(), screen.right() - width));
    int y = cursor.bottom();
    if (y + height > screen.bottom()) y = cursor.top() - height;
    return {x, std::clamp(y, screen.top(),
                          std::max(screen.top(), screen.bottom() - height))};
}
}
