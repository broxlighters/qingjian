//! XWayland 全局 root 映射只能由全部输出几何共同验证，混合倍率不读 Xft 猜测。
#include "panel/backend/scale.h"
#include <cassert>
using fcitx::Rect;
int main() {
    const std::vector<Rect> roots = {Rect(0, 0, 3456, 2160), Rect(3456, 0, 7296, 2160)};
    const std::vector<Rect> logical = {Rect(0, 0, 1728, 1080), Rect(1728, 0, 3648, 1080)};
    assert(qingjian::panel::xwaylandRaster(roots, logical) == 2.0);
    assert(!qingjian::panel::xwaylandRaster(roots, {Rect(0, 0, 2880, 1800), Rect(2880, 0, 4800, 1080)}));
    assert(!qingjian::panel::xwaylandRaster(roots, {logical.front()}));
    assert(!qingjian::panel::xwaylandRaster({}, {}));
}
