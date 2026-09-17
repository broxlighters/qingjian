//! Render ABI v1：调用者串行使用句柄；像素为预乘 RGBA8；Rust 分配须用配套 destroy。
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define QJ_RENDER_ABI_VERSION 1
#define QJ_RENDER_SIZE_ABI_VERSION 1
struct QjRenderer;
struct QjResult;
typedef struct {
    uint32_t width, height, stride;
    uint64_t length;
    const uint8_t *pixels;
    uint32_t truncated;
    uint64_t layout_ns, raster_ns;
} QjImageInfo;
uint32_t qj_render_abi_version(void);
struct QjRenderer *qj_renderer_create(uint32_t version);
void qj_renderer_destroy(struct QjRenderer *renderer);
void qj_renderer_clear_text_cache(struct QjRenderer *renderer);
struct QjResult *qj_renderer_render(struct QjRenderer *, uint32_t version, const uint8_t *json, size_t length, float scale, uint32_t width, uint32_t height, uint32_t dark);
struct QjResult *qj_renderer_render_sized(struct QjRenderer *, uint32_t version, uint32_t size_version, const uint8_t *json, size_t length, float raster_scale, float ui_scale, float text_scale, uint32_t width, uint32_t height, uint32_t dark);
bool qj_result_image(const struct QjResult *, QjImageInfo *out);
int32_t qj_result_page(const struct QjResult *, uint32_t x, uint32_t y);
int32_t qj_result_hit(const struct QjResult *, uint32_t x, uint32_t y);
bool qj_result_exposure(const struct QjResult *, uint32_t index, uint32_t *row, uint32_t *sense);
void qj_result_destroy(struct QjResult *);
#ifdef __cplusplus
}
#endif
