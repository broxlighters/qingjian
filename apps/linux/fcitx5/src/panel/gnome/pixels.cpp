//! 密封 memfd 与当前纹理的命中结果；只有 Painted 对当前帧回报完整义项。
#include "panel.h"
#include "../interaction/action.h"
#include "../status.h"
#include "qingjian_render.h"
#include <fcitx-utils/unixfd.h>
#include <fcitx-utils/log.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
namespace qingjian::panel {
void GnomePanel::upload(double raster, int width, int height) {
    const auto body = current_->frame.dump();
    preparing_.reset(qj_renderer_render_sized(current_->renderer.get(), QJ_RENDER_ABI_VERSION,
        QJ_RENDER_SIZE_ABI_VERSION, reinterpret_cast<const uint8_t *>(body.data()), body.size(),
        raster, current_->size.uiScale, current_->size.textScale(current_->textScale), width, height,
        current_->dark), qj_result_destroy);
    QjImageInfo image{};
    if (preparing_ && qj_result_image(preparing_.get(), &image) && image.width == 1 && image.height == 1) {
        hide(); return;
    }
    if (!preparing_ || !qj_result_image(preparing_.get(), &image) ||
        image.width > 1600 || image.height > 900 || image.stride != image.width * 4 ||
        image.length != size_t(image.stride) * image.height || image.length > 5760000) {
        fail("buffer_invalid"); return;
    }
    auto fd = fcitx::UnixFD::own(memfd_create("qingjian-frame", MFD_CLOEXEC | MFD_ALLOW_SEALING));
    if (fd.fd() < 0) { fail("buffer_invalid"); return; }
    size_t offset = 0;
    while (offset < image.length) {
        const auto count = write(fd.fd(), image.pixels + offset, image.length - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { fail("buffer_invalid"); return; }
        offset += count;
    }
    if (lseek(fd.fd(), 0, SEEK_SET) != 0 || fcntl(fd.fd(), F_ADD_SEALS,
        F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL) < 0) { fail("buffer_invalid"); return; }
    auto prepare = bridge_->message("PrepareFrame");
    recordStatus({{"coordinates", "client-relative"}, {"raster_scale", raster}, {"raster_source", "gnome-monitor"},
        {"pixel_size", {image.width, image.height}}, {"logical_size", {image.width / raster, image.height / raster}}});
    auto regions = nlohmann::json::array();
    QjInteractionRegion region{};
    for (uint32_t index = 0; index < 130 && qj_result_region(preparing_.get(), index, &region); ++index)
        regions.push_back({region.x, region.y, region.width, region.height, region.action});
    const auto interactions = nlohmann::json({{"regions", regions}, {"previous", current_->frame.value("page", 0U) > 0},
        {"next", current_->frame.value("page", 0U) + 1 < current_->frame.value("page_count", 1U)},
        {"ui_scale", current_->size.uiScale}, {"text_scale", current_->size.textScale(current_->textScale)},
        {"system_text_scale", current_->textScale}, {"ui_source", "linux_ui.ui_scale_percent"},
        {"system_text_source", current_->textScaleKnown ? "portal" : "default-unavailable"},
        {"text_source", !current_->size.followSystemTextScale ? "user-disabled" : current_->textScaleKnown ? "portal" : "default-unavailable"}}).dump();
    prepare << token_ << fd << image.width << image.height << image.stride << uint32_t(image.length)
        << double(image.width / raster) << double(image.height / raster) << interactions;
    request(std::move(prepare), [this](auto &) {
        if (!valid()) { hide(); return; }
        state_ = DisplayState::AwaitingPaint;
        auto show = bridge_->message("Show"); show << token_;
        request(std::move(show), [](auto &) {});
    });
}
void GnomePanel::painted() {
    if (!valid() || state_ != DisplayState::AwaitingPaint || !preparing_) return;
    visible_ = std::move(preparing_);
    state_ = DisplayState::Visible;
    recordStatus({{"backend", "gnome"}, {"state", "Visible"}, {"painted", true}, {"reason", nullptr}});
    deadline_ = 0;
    failure_.clear();
    if (timer_) { timer_->setTime(fcitx::now(CLOCK_MONOTONIC) + 100000); timer_->setEnabled(true); }
    auto senses = nlohmann::json::array();
    uint32_t row = 0, sense = 0;
    for (uint32_t index = 0; index < 128 && qj_result_exposure(visible_.get(), index, &row, &sense); ++index)
        senses.push_back({row, sense});
    auto callback = current_->acknowledge;
    if (callback) callback(senses);
}
void GnomePanel::pointer(fcitx::dbus::Message &message) {
    if (!valid() || state_ != DisplayState::Visible || !visible_ || latest_) return;
    uint32_t x = 0, y = 0, button = 0;
    message >> x >> y >> button;
    if (!message || x > 1600 || y > 900) return;
    const auto row = qj_result_hit(visible_.get(), x, y);
    if (button == 1 && row < 0) {
        const auto page = qj_result_page(visible_.get(), x, y);
        if (page) button = page < 0 ? 4 : 5;
    }
    if (auto mapped = action(button, row, current_->frame.value("page", 0U) > 0,
        current_->frame.value("page", 0U) + 1 < current_->frame.value("page_count", 1U))) {
        // 同批连续点击在调用已有按键入口前作废。
        state_ = DisplayState::Preparing;
        auto callback = current_->action;
        if (callback) callback(*mapped);
    }
}
}
