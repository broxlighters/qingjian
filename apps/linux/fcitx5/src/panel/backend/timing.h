//! 提交阶段的本机单调时钟耗时；commit 只表示 X server 接受，不表示已扫描到屏幕。
#pragma once
#include <cstdint>
namespace qingjian::panel {
struct SubmissionTiming {
    uint64_t probeNs = 0;

    uint64_t convertNs = 0;

    uint64_t uploadNs = 0;

    uint64_t commitNs = 0;

    uint64_t uploadBytes = 0;
};
}
