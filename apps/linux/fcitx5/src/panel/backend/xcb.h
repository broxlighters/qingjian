//! XCB 实验窗口工厂；普通构建不开放未经桌面验收的承载路线。
#pragma once
#include "base.h"
#include <memory>
#include <string>
namespace qingjian::panel {
std::unique_ptr<Backend> openXcb(const std::string &display);
}
