//! 异步后端提交结果；Pending 不触发默认面板曝光或表示已经可见。
#pragma once
namespace qingjian::panel {
enum class Submission { Pending, Accepted, Failed };
}
