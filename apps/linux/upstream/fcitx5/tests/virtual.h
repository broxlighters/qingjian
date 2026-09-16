//! 仅测试使用的内部虚拟上下文夹具；生产消费者不编译此文件。
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <memory>
namespace fcitx { class InputContext; class InputContextManager; class AddonInstance; }
class VirtualFixture {
public:
    explicit VirtualFixture(fcitx::InputContext *parent, fcitx::InputContextManager &manager);
    ~VirtualFixture();
    fcitx::InputContext *focus();
    void clear();
private:
    struct Data;
    std::unique_ptr<Data> data_;
};

void testWrongVirtualParent(fcitx::AddonInstance *, fcitx::InputContextManager &);
