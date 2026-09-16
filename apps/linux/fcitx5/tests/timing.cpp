//! 固定测试语句的真实 XCB 提交计时；需显式 display，不操作用户输入法或记录真实输入。
#include "panel/backend/xcb.h"
#include "qingjian_render.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>
namespace {
using Clock = std::chrono::steady_clock;
uint64_t elapsed(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}
double percentile(std::vector<uint64_t> values, size_t percentage) {
    std::sort(values.begin(), values.end());
    return values[(values.size() * percentage + 99) / 100 - 1] / 1000000.0;
}
nlohmann::json frame(bool longText, const char *layout) {
    auto result = nlohmann::json::parse(R"({"preedit":[{"text":"nihao","kind":"Typed"}],"cursor":5,"candidates":{"items":[]},"highlight":0,"page":0,"page_count":2,"layout":"vertical","theme":"light","sentence":null,"notice":null})");
    result["layout"] = layout;
    for (unsigned row = 0; row < (longText ? 8U : 3U); ++row) {
        auto item = nlohmann::json::parse(R"({"text":"你好","kind":"Chinese","syllables":[],"reading":null,"translation":{"language":"English","senses":[{"text":"greeting","part_of_speech":null,"reading":null,"fresh":true}]}})");
        if (longText) {
            item["text"] = "青简输入法候选面板你好世界";
            item["translation"]["senses"][0]["text"] = "a fixed long phrase for measuring candidate rendering and upload";
        }
        result["candidates"]["items"].push_back(std::move(item));
    }
    return result;
}
}
int main(int argc, char **argv) try {
    if (argc != 2 || !*argv[1]) {
        fprintf(stderr, "用法：qingjian-xcb-timing X-display；仅测试自己创建的窗口\n");
        return 2;
    }
    auto backend = qingjian::panel::openXcb(argv[1]);
    if (!backend) { fprintf(stderr, "X11 合成器、ARGB 或 RandR 条件不满足\n"); return 77; }
    const auto init = Clock::now();
    const std::unique_ptr<QjRenderer, decltype(&qj_renderer_destroy)> renderer(qj_renderer_create(QJ_RENDER_ABI_VERSION), qj_renderer_destroy);
    if (!renderer) throw std::runtime_error("renderer");
    fprintf(stderr, "系统字体初始化 %.3f ms；commit 是 X server 已接受，不是屏幕可见时间；不含 IPC\n", elapsed(init) / 1000000.0);
    const auto area = backend->bounds(fcitx::Rect(100, 100, 101, 120));
    const fcitx::Rect cursor(area.left() + 10, area.top() + 10, area.left() + 11, area.top() + 30);
    puts("sample,layout,scale,change,width,height,first_ms,total_p50_ms,total_p95_ms,total_p99_ms,layout_p95_ms,raster_p95_ms,backend_p95_ms,probe_p95_ms,convert_p95_ms,upload_p95_ms,commit_p95_ms,mean_upload_bytes");
    for (bool longText : {false, true}) for (const auto *layout : {"vertical", "horizontal"}) {
        for (float scale : {1.0F, 1.25F, 1.5F, 2.0F}) for (const auto *change : {"same", "highlight", "page"}) {
            auto source = frame(longText, layout);
            std::vector<uint64_t> total, layouts, rasters, backends, probes, conversions, uploads, commits;
            uint64_t bytes = 0, first = 0;
            QjImageInfo image{};
            for (unsigned iteration = 0; iteration <= 100; ++iteration) {
                if (change == std::string("highlight")) source["highlight"] = iteration % 2;
                if (change == std::string("page")) source["page"] = iteration % 2;
                backend->hide(); // 与 Controller 同样先隐藏、后交付新帧。
                const auto started = Clock::now();
                const auto body = source.dump();
                const std::unique_ptr<QjResult, decltype(&qj_result_destroy)> result(qj_renderer_render(renderer.get(), QJ_RENDER_ABI_VERSION,
                    reinterpret_cast<const uint8_t *>(body.data()), body.size(), scale,
                    std::min(area.width(), 1600), std::min(area.height(), 900), 0), qj_result_destroy);
                if (!result || !qj_result_image(result.get(), &image)) throw std::runtime_error("render");
                const auto startBackend = Clock::now();
                if (!backend->present(image.pixels, image.width, image.height, image.stride, cursor)) throw std::runtime_error("present");
                const auto backendNs = elapsed(startBackend), totalNs = elapsed(started);
                if (!iteration) { first = totalNs; continue; }
                const auto timing = backend->timing();
                total.push_back(totalNs); layouts.push_back(image.layout_ns); rasters.push_back(image.raster_ns);
                backends.push_back(backendNs); probes.push_back(timing.probeNs); conversions.push_back(timing.convertNs);
                uploads.push_back(timing.uploadNs); commits.push_back(timing.commitNs); bytes += timing.uploadBytes;
            }
            printf("%s,%s,%.2f,%s,%u,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%llu\n",
                longText ? "long" : "short", layout, scale, change, image.width, image.height, first / 1000000.0,
                percentile(total, 50), percentile(total, 95), percentile(total, 99), percentile(layouts, 95), percentile(rasters, 95),
                percentile(backends, 95), percentile(probes, 95), percentile(conversions, 95), percentile(uploads, 95), percentile(commits, 95),
                static_cast<unsigned long long>(bytes / total.size()));
            fflush(stdout);
        }
    }
    backend->hide();
} catch (const std::exception &) {
    fprintf(stderr, "X11 计时失败；结果不能用作验收记录\n");
    return 1;
}
