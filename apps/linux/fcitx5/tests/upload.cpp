//! 差分上传须保持颜色、alpha、stride 与尺寸切换的完整性，并支持失败后强制重传。
#include "panel/backend/upload.h"
#include <array>
#include <cassert>
#include <cstring>
int main() {
    qingjian::panel::PixelUpload upload;
    std::array<uint8_t, 48> rgba{}; // 2×4，8 字节有效行 + 4 字节 padding。
    for (unsigned row = 0; row < 4; ++row) {
        for (unsigned column = 0; column < 2; ++column) {
            auto *pixel = rgba.data() + row * 12 + column * 4;
            pixel[0] = row * 10; pixel[1] = 30; pixel[2] = 70; pixel[3] = 128;
        }
    }
    assert(upload.prepare(rgba.data(), 2, 4, 12, false));
    assert(upload.firstRow() == 0 && upload.rowCount() == 4);
    assert(upload.data()[0] == 70 && upload.data()[1] == 30 && upload.data()[2] == 0 && upload.data()[3] == 128);
    assert(upload.prepare(rgba.data(), 2, 4, 12, false) && upload.rowCount() == 0);
    rgba[8] = 255; // padding 改动不能产生无意义上传。
    assert(upload.prepare(rgba.data(), 2, 4, 12, false) && upload.rowCount() == 0);
    rgba[12 + 3] = 255; // alpha 单独变化也必须上传。
    assert(upload.prepare(rgba.data(), 2, 4, 12, false));
    assert(upload.firstRow() == 1 && upload.rowCount() == 1 && upload.data()[8 + 3] == 255);
    rgba[0] = 50; rgba[3 * 12 + 2] = 90;
    assert(upload.prepare(rgba.data(), 2, 4, 12, false));
    assert(upload.firstRow() == 0 && upload.rowCount() == 4);
    assert(upload.prepare(rgba.data(), 2, 4, 12, true) && upload.rowCount() == 4);
    assert(upload.prepare(rgba.data(), 1, 4, 12, false) && upload.rowCount() == 4);
    assert(!upload.prepare(nullptr, 1, 4, 12, false));
    assert(!upload.prepare(rgba.data(), 1601, 4, 6404, false));
    assert(!upload.prepare(rgba.data(), 1, 901, 12, false));
    assert(!upload.prepare(rgba.data(), 2, 4, 7, false));
    assert(upload.prepare(rgba.data(), 1, 4, 12, false) && upload.rowCount() == 0);
}
