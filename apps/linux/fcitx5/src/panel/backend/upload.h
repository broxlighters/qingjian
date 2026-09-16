//! X11 上传格式转换与差分行范围；只保留一张有界的预乘 BGRA 位图。
#pragma once
#include <cstdint>
#include <vector>
namespace qingjian::panel {
class PixelUpload final {
public:
    bool prepare(const uint8_t *rgba, uint32_t width, uint32_t height, uint32_t stride, bool force);
    const uint8_t *data() const { return pixels_.data(); }
    uint32_t firstRow() const { return first_; }
    uint32_t rowCount() const { return count_; }
private:
    std::vector<uint8_t> pixels_;

    uint32_t width_ = 0, height_ = 0;

    /// 相同尺寸只上传第一个至最后一个变化行；完全相同的帧不上传像素。
    uint32_t first_ = 0, count_ = 0;
};
}
