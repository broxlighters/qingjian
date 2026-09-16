//! 仅渲染的 C ABI v1；句柄线程串行，所有 Rust panic 在边界捕获。
mod info;
mod result;
pub use info::ImageInfo;
use qingjian_render::{Frame, RenderConfig, Renderer, Theme};
pub use result::RenderResult;
use std::panic::{AssertUnwindSafe, catch_unwind};

pub const ABI_VERSION: u32 = 1;
const MAX_FRAME_JSON: usize = 262_144;

#[unsafe(no_mangle)]
pub extern "C" fn qj_render_abi_version() -> u32 {
    ABI_VERSION
}

/// 创建字体库，可能较慢，应在后台启用前初始化。
#[unsafe(no_mangle)]
pub extern "C" fn qj_renderer_create(version: u32) -> *mut Renderer {
    if version != ABI_VERSION {
        return std::ptr::null_mut();
    }
    catch_unwind(|| {
        Renderer::new()
            .ok()
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
    if handle.is_null()
        || version != ABI_VERSION
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
        let frame = Frame::from(&protocol);
        let config = RenderConfig {
            scale,
            max_width: width,
            max_height: height,
            system_theme: if dark == 1 {
                Theme::dark()
            } else {
                Theme::light()
            },
        };
        let (frame, timing) = unsafe { &mut *handle }.render_timed(&frame, config).ok()?;
        Some(Box::into_raw(Box::new(RenderResult { frame, timing })))
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
                width: result.frame.width,
                height: result.frame.height,
                stride: result.frame.stride,
                length: result.frame.pixels.len() as u64,
                pixels: result.frame.pixels.as_ptr(),
                truncated: u32::from(result.frame.truncated),
                layout_ns: result.timing.layout_ns,
                raster_ns: result.timing.raster_ns,
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
        let Some(item) = unsafe { &*result }.frame.exposures.get(index as usize) else {
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
