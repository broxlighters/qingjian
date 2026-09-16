//! Rust 分配的位图与曝光索引；句柄由配套 C ABI 销毁。
use crate::exposure::Exposure;
use qingjian_render::RenderedPanel;
pub struct RenderResult {
    pub(crate) frame: RenderedPanel,

    pub(crate) exposures: Vec<Exposure>,
}
