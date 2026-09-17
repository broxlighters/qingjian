//! 连续帧准备未完成时撤窗，避免渲染错误留下旧候选。
#pragma once
#include <functional>
namespace qingjian::panel {
class UpdateGuard final {
public:
    explicit UpdateGuard(std::function<void()> cancel) : cancel_(std::move(cancel)) {}
    ~UpdateGuard() { if (cancel_) cancel_(); }
    void accepted() { cancel_ = {}; }
private:
    std::function<void()> cancel_;
};
}
