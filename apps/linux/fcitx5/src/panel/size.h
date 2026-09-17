//! 分开保留栅格倍率与用户/文字偏好；不借 Xft DPI 猜测输出缩放。
#pragma once
#include <cmath>
#include <nlohmann/json.hpp>

namespace qingjian::panel {
struct SizeOptions {
    double uiScale = 1.0;

    bool followSystemTextScale = true;

    static SizeOptions fromSettings(const nlohmann::json &settings) {
        SizeOptions result;
        if (!settings.is_object()) return result;
        const auto version = settings.find("size_version");
        if (version == settings.end() || !version->is_number_integer() || *version != 1) return result;
        const auto found = settings.find("ui_scale_percent");
        if (found != settings.end() && found->is_number_integer()) {
            const auto value = found->get<int64_t>();
            if (value >= 75 && value <= 200) result.uiScale = static_cast<double>(value) / 100;
        }
        const auto follow = settings.find("follow_system_text_scale");
        if (follow != settings.end() && follow->is_boolean()) result.followSystemTextScale = follow->get<bool>();
        return result;
    }

    double textScale(double system) const {
        return followSystemTextScale && std::isfinite(system) && system >= 0.5 && system <= 3.0 ? system : 1.0;
    }
};
}
