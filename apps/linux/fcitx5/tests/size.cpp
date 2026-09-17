//! 尺寸能力协商、旧 Server 默认值与系统文字偏好开关。
#include "panel/size.h"
#include <cassert>
#include <limits>
int main() {
    using qingjian::panel::SizeOptions;
    assert(SizeOptions::fromSettings({}).uiScale == 1.0);
    assert(SizeOptions::fromSettings({{"ui_scale_percent", 200}}).uiScale == 1.0);
    auto options = SizeOptions::fromSettings({{"size_version", 1}, {"ui_scale_percent", 125}});
    assert(options.uiScale == 1.25 && options.textScale(1.25) == 1.25);
    for (int value : {74, 201, -1}) {
        assert(SizeOptions::fromSettings({{"size_version", 1}, {"ui_scale_percent", value}}).uiScale == 1.0);
    }
    assert(options.textScale(std::numeric_limits<double>::quiet_NaN()) == 1.0);
    options.followSystemTextScale = false;
    assert(options.textScale(1.25) == 1.0);
}
