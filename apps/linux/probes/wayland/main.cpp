//! 只打印承载能力；可选固定色块，不连接青简 Server 或读取候选内容。
#include "probe.h"
#include <cstdio>
#include <cstring>

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3 || (argc == 3 && std::strcmp(argv[2], "--surface") != 0)) {
        std::fprintf(stderr, "用法：qingjian-wayland-probe WAYLAND_SOCKET [--surface]\n");
        return 2;
    }
    qingjian::probe::Probe probe;
    return probe.run(argv[1], argc == 3);
}
