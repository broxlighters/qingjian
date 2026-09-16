//! 承载工厂只接受上下文 display；生产 Wayland 路径受阶段 0 决策门约束。
#pragma once
#include "base.h"
#include <memory>
#include <string>
namespace qingjian::panel {
std::unique_ptr<Backend> openBackend(const std::string &display, const char **reason = nullptr);
}
