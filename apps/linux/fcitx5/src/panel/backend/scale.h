//! 用完整输出几何验证 XWayland root→Shell逻辑映射；不把 Xft DPI 或客户端 scale 当屏幕倍率。
#pragma once
#include <fcitx-utils/rect.h>
#include <cmath>
#include <optional>
#include <vector>
namespace qingjian::panel {
inline std::optional<double> xwaylandRaster(const std::vector<fcitx::Rect> &roots,
                                           const std::vector<fcitx::Rect> &logical) {
    if (roots.empty() || roots.size() != logical.size()) return {};
    std::optional<double> resolved;
    for (const auto &candidate : logical) {
        if (candidate.width() <= 0 || candidate.height() <= 0) continue;
        const double ratio = double(roots.front().width()) / candidate.width();
        if (ratio < 0.5 || ratio > 4) continue;
        std::vector<bool> matched(logical.size());
        bool complete = true;
        for (const auto &root : roots) {
            bool found = false;
            for (size_t index = 0; index < logical.size(); ++index) {
                const auto &area = logical[index];
                const auto close = [ratio](int physical, int stage) { return std::abs(physical - stage * ratio) <= 2; };
                if (!matched[index] && close(root.left(), area.left()) && close(root.top(), area.top()) &&
                    close(root.width(), area.width()) && close(root.height(), area.height())) {
                    matched[index] = true; found = true; break;
                }
            }
            if (!found) { complete = false; break; }
        }
        if (complete) {
            if (resolved && std::abs(*resolved - ratio) > 0.001) return {};
            resolved = ratio;
        }
    }
    return resolved;
}
}
