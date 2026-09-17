//! 仅渲染的 C ABI v1；句柄线程串行，所有 Rust panic 在边界捕获。
mod display;
mod exposure;
mod info;
mod region;
mod result;
mod sense;
mod size;
use display::DisplayFrame;
use exposure::Exposure;
pub use info::ImageInfo;
use qingjian_platform::{LayoutMode, ThemeMode};
use qingjian_render::{FontLibrary, Layout, PanelConfig, Renderer, Theme};
pub use result::RenderResult;
use std::panic::{AssertUnwindSafe, catch_unwind};

pub const ABI_VERSION: u32 = 1;
pub const SIZE_ABI_VERSION: u32 = 1;
const MAX_FRAME_JSON: usize = 262_144;

#[unsafe(no_mangle)]
pub extern "C" fn qj_render_abi_version() -> u32 {
    ABI_VERSION
}

/// 清理含文字的整形缓存，不影响已返回的独立结果句柄。
/// # Safety
/// handle 为有效独占 Renderer，或 null；不得并发使用。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn qj_renderer_clear_text_cache(handle: *mut Renderer) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if let Some(renderer) = unsafe { handle.as_mut() } {
            renderer.clear_text_cache();
        }
    }));
}

/// 创建字体库，可能较慢，应在后台启用前初始化。
#[unsafe(no_mangle)]
pub extern "C" fn qj_renderer_create(version: u32) -> *mut Renderer {
    if version != ABI_VERSION {
        return std::ptr::null_mut();
    }
    catch_unwind(|| {
        FontLibrary::system("zh-CN")
            .ok()
            .map(Renderer::new)
            .map(|r| Box::into_raw(Box::new(r)))
            .unwrap_or_default()
    })
    .unwrap_or_default()
}

/// # Safety
/// handle 必须为本库创建且未销毁的唯一 Renderer，或 null；不得并发使用。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn qj_renderer_destroy(handle: *mut Renderer) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if !handle.is_null() {
            drop(unsafe { Box::from_raw(handle) });
        }
    }));
}

/// 接收现有 platform::protocol::Frame JSON，不创建 Engine 或访问词库。
/// # Safety
/// handle 为有效的独占句柄；json 指向 length 个可读字节，整个调用内有效。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn qj_renderer_render(
    handle: *mut Renderer,
    version: u32,
    json: *const u8,
    length: usize,
    scale: f32,
    width: u32,
    height: u32,
    dark: u32,
) -> *mut RenderResult {
    unsafe {
        qj_renderer_render_sized(
            handle,
            version,
            SIZE_ABI_VERSION,
            json,
            length,
            scale,
            1.0,
            1.0,
            width,
            height,
            dark,
        )
    }
}

/// Linux 尺寸扩展；保留 ABI v1 原入口的单位大小行为。
/// # Safety
/// 句柄和 JSON 的有效性要求与 qj_renderer_render 一致。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn qj_renderer_render_sized(
    handle: *mut Renderer,
    version: u32,
    size_version: u32,
    json: *const u8,
    length: usize,
    scale: f32,
    ui_scale: f32,
    text_scale: f32,
    width: u32,
    height: u32,
    dark: u32,
) -> *mut RenderResult {
    if handle.is_null()
        || version != ABI_VERSION
        || size_version != SIZE_ABI_VERSION
        || json.is_null()
        || length == 0
        || length > MAX_FRAME_JSON
        || dark > 1
    {
        return std::ptr::null_mut();
    }
    catch_unwind(AssertUnwindSafe(|| {
        let bytes = unsafe { std::slice::from_raw_parts(json, length) };
        let protocol: qingjian_platform::protocol::Frame = serde_json::from_slice(bytes).ok()?;
        let display = DisplayFrame::new(&protocol);
        let config = PanelConfig {
            scale,
            max_width: width,
            max_height: height,
            layout: match protocol.layout {
                LayoutMode::Vertical => Layout::Vertical,
                LayoutMode::Horizontal => Layout::Horizontal,
            },
            theme: size::scaled_theme(
                match protocol.theme {
                    ThemeMode::Dark => Theme::dark(),
                    ThemeMode::Light => Theme::light(),
                    ThemeMode::System => {
                        if dark == 1 {
                            Theme::dark()
                        } else {
                            Theme::light()
                        }
                    }
                },
                ui_scale,
                text_scale,
            )?,
            page: protocol.page,
            page_count: protocol.page_count,
        };
        let frame = unsafe { &mut *handle }
            .render_panel(&display.frame, &config)
            .ok()?;
        let exposures = display
            .senses
            .iter()
            .filter(|sense| {
                !sense.segments.is_empty()
                    && sense.segments.clone().all(|segment| {
                        frame
                            .image
                            .geometry
                            .annotations
                            .contains(&(sense.row, segment))
                    })
            })
            .map(|sense| Exposure {
                row: sense.row,
                sense: sense.sense,
            })
            .collect();
        Some(Box::into_raw(Box::new(RenderResult { frame, exposures })))
    }))
    .ok()
    .flatten()
    .unwrap_or_default()
}

/// # Safety
/// result 为有效句柄；out 指向一个可写 ImageInfo。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn qj_result_image(result: *const RenderResult, out: *mut ImageInfo) -> bool {
    if result.is_null() || out.is_null() {
        return false;
    }
    catch_unwind(AssertUnwindSafe(|| {
        let result = unsafe { &*result };
        unsafe {
            *out = ImageInfo {
                width: result.frame.image.pixmap.width(),
                height: result.frame.image.pixmap.height(),
                stride: result.frame.image.pixmap.width() * 4,
                length: result.frame.image.pixmap.data().len() as u64,
                pixels: result.frame.image.pixmap.data().as_ptr(),
                truncated: u32::from(result.frame.truncated),
                layout_ns: result.frame.timing.layout_ns,
                raster_ns: result.frame.timing.raster_ns,
            };
        }
        true
    }))
    .unwrap_or(false)
}

/// 返回候选原槽位，-1 代表无命中。
/// # Safety
/// result 是有效句柄或 null。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn qj_result_hit(result: *const RenderResult, x: u32, y: u32) -> i32 {
    if result.is_null() {
        return -1;
    }
    catch_unwind(AssertUnwindSafe(|| {
        unsafe { &*result }
            .frame
            .image
            .geometry
            .hit_test(x, y)
            .map_or(-1, |i| i as i32)
    }))
    .unwrap_or(-1)
}

/// 返回上一页 -1、下一页 1、未命中 0；不改变候选槽位。
/// # Safety
/// result 是有效句柄或 null。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn qj_result_page(result: *const RenderResult, x: u32, y: u32) -> i32 {
    if result.is_null() {
        return 0;
    }
    catch_unwind(AssertUnwindSafe(|| {
        let frame = &unsafe { &*result }.frame;
        if frame.previous_page.is_some_and(|r| r.contains(x, y)) {
            -1
        } else if frame.next_page.is_some_and(|r| r.contains(x, y)) {
            1
        } else {
            0
        }
    }))
    .unwrap_or(0)
}

/// 顺序读取已绘制交互区域；不包含阴影、空槽或无效翻页按钮。
/// # Safety
/// result 有效；out 指向可写 InteractionRegion，或任一参数为 null。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn qj_result_region(
    result: *const RenderResult,
    index: u32,
    out: *mut region::InteractionRegion,
) -> bool {
    if result.is_null() || out.is_null() {
        return false;
    }
    catch_unwind(AssertUnwindSafe(|| {
        let frame = &unsafe { &*result }.frame;
        let found = frame
            .image
            .geometry
            .candidates
            .iter()
            .map(|hit| (hit.rect, hit.row as i32))
            .chain(frame.previous_page.map(|rect| (rect, -1)))
            .chain(frame.next_page.map(|rect| (rect, -2)))
            .nth(index as usize);
        let Some((rect, action)) = found else {
            return false;
        };
        unsafe {
            *out = region::InteractionRegion {
                x: rect.x,
                y: rect.y,
                width: rect.width,
                height: rect.height,
                action,
            };
        }
        true
    }))
    .unwrap_or(false)
}

/// 顺序读取完整可见的义项，越界返回 false。
/// # Safety
/// result 有效；row/sense 指向可写 u32。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn qj_result_exposure(
    result: *const RenderResult,
    index: u32,
    row: *mut u32,
    sense: *mut u32,
) -> bool {
    if result.is_null() || row.is_null() || sense.is_null() {
        return false;
    }
    catch_unwind(AssertUnwindSafe(|| {
        let Some(item) = unsafe { &*result }.exposures.get(index as usize) else {
            return false;
        };
        unsafe {
            *row = item.row as u32;
            *sense = item.sense as u32;
        }
        true
    }))
    .unwrap_or(false)
}

/// # Safety
/// result 来自 qj_renderer_render 且尚未释放，或 null；所有借用像素失效。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn qj_result_destroy(result: *mut RenderResult) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if !result.is_null() {
            drop(unsafe { Box::from_raw(result) });
        }
    }));
}
