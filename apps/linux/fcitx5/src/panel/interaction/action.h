//! 位图事件只能映射到已有数字选择与翻页通路。
#pragma once
#include <optional>
namespace qingjian::panel {
inline std::optional<int> action(unsigned button, int row, bool previous, bool next) {
    if (button == 1 && row >= 0 && row < 9) return row;
    if (button == 4 && previous) return -1;
    if (button == 5 && next) return -2;
    return std::nullopt;
}
}
