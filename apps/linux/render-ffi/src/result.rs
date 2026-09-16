//! Rust 分配的位图与指标句柄；只通过配套 FFI 销毁。
use qingjian_render::{RenderTiming, RenderedFrame};
pub struct RenderResult {
    /// 预乘 RGBA 与候选/义项区域。
    pub(crate) frame: RenderedFrame,

    /// 排版和绘制耗时。
    pub(crate) timing: RenderTiming,
}
