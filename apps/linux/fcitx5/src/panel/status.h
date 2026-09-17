//! 只读诊断的非敏感状态快照；不保存帧、光标坐标或输入文本。
#pragma once
#include <nlohmann/json.hpp>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
namespace qingjian::panel {
inline void recordStatus(const nlohmann::json &update) {
    static nlohmann::json status = nlohmann::json::object();
    static std::string previous;
    for (const auto &[key, value] : update.items()) status[key] = value;
    status["pid"] = getpid();
    const auto body = status.dump();
    if (body == previous || body.size() > 16384) return;
    const char *runtime = std::getenv("XDG_RUNTIME_DIR");
    if (!runtime || !*runtime) return;
    struct stat info{};
    if (stat(runtime, &info) != 0 || !S_ISDIR(info.st_mode) || info.st_uid != getuid() || (info.st_mode & 0022)) return;
    const auto path = std::string(runtime) + "/qingjian-ui-" + std::to_string(getpid()) + ".json";
    const auto pattern = path + ".XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end()); name.push_back(0);
    const int fd = mkstemp(name.data());
    if (fd < 0) return;
    size_t offset = 0;
    while (offset < body.size()) {
        const auto written = write(fd, body.data() + offset, body.size() - offset);
        if (written <= 0) break;
        offset += written;
    }
    close(fd);
    if (offset == body.size() && rename(name.data(), path.c_str()) == 0) previous = body;
    else unlink(name.data());
}
}
