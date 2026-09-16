//! 像素比较与转换共用一次遍历，stride 的 padding 不参与差分。
#include "upload.h"
#include <algorithm>
#include <cstddef>
namespace qingjian::panel {
bool PixelUpload::prepare(const uint8_t *rgba, uint32_t width, uint32_t height, uint32_t stride, bool force) {
    if (!rgba || !width || !height || width > 1600 || height > 900 || stride < width * 4) return false;
    const bool full = force || width != width_ || height != height_;
    pixels_.resize(static_cast<size_t>(width) * height * 4);
    first_ = height;
    uint32_t end = 0;
    for (uint32_t row = 0; row < height; ++row) {
        bool changed = full;
        for (uint32_t col = 0; col < width; ++col) {
            const auto *source = rgba + static_cast<size_t>(row) * stride + col * 4;
            auto *dest = pixels_.data() + (static_cast<size_t>(row) * width + col) * 4;
            changed |= dest[0] != source[2] || dest[1] != source[1] || dest[2] != source[0] || dest[3] != source[3];
            dest[0] = source[2]; dest[1] = source[1]; dest[2] = source[0]; dest[3] = source[3];
        }
        if (changed) { first_ = std::min(first_, row); end = row + 1; }
    }
    count_ = end ? end - first_ : 0;
    width_ = width;
    height_ = height;
    return true;
}
}
