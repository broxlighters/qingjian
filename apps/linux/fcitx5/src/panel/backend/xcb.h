//! X11 非激活候选窗工厂；没有 XCB 开发包时可从最小构建中移除。
#pragma once
#include "base.h"
#include <memory>
#include <string>
namespace qingjian::panel {
std::unique_ptr<Backend> openXcb(const std::string &display, const char **reason = nullptr);
}
