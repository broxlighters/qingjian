//! 通过管道分发鼠标事件的无窗口承载，用于控制器回归。
#pragma once
#include "panel/backend/base.h"
#include <array>
#include <cassert>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>
#include <vector>
class MemoryBackend final : public qingjian::panel::Backend {
public:
    MemoryBackend() { assert(pipe2(pipe_, O_NONBLOCK) == 0); }
    ~MemoryBackend() override { close(pipe_[0]); close(pipe_[1]); }
    fcitx::Rect bounds(const fcitx::Rect &) override { return fcitx::Rect(0, 0, 1920, 1080); }
    bool present(const uint8_t *rgba, uint32_t width, uint32_t height, uint32_t stride, const fcitx::Rect &) override {
        assert(rgba && width > 1 && height > 1 && stride == width * 4);
        if (throwing) throw std::runtime_error("injected upload failure");
        shown = !fail;
        return shown;
    }
    void hide() override { shown = false; }
    void drain() override {
        char signal;
        while (read(pipe_[0], &signal, 1) == 1) {}
        pending.clear();
    }
    void enqueue(std::array<int, 3> event) {
        pending.push_back(event);
        assert(write(pipe_[1], "x", 1) == 1);
    }
    bool poll(const std::function<bool(int, int, unsigned)> &click) override {
        auto events = pending;
        drain();
        for (const auto &event : events)
            if (!click(event[0], event[1], event[2])) break;
        if (afterPoll) afterPoll();
        return true;
    }
    int fd() const override { return pipe_[0]; }
    bool healthy() override { return healthyConnection; }
    bool healthyConnection = true;

    /// 当前是否成功提交位图。
    bool shown = false;

    /// 注入贴图失败，不影响真实渲染结果。
    bool fail = false;

    /// 注入 C++ 承载异常，确保异常不会穿过 Fcitx 事件回调。
    bool throwing = false;

    /// 在事件循环分发完一批事件后结束测试循环。
    std::function<void()> afterPoll;
private:
    /// 当前帧的一批鼠标事件。
    std::vector<std::array<int, 3>> pending;

    /// 供事件循环注册的无窗口管道。
    int pipe_[2]{};
};
