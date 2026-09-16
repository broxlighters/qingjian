//! 限制窗口物理尺寸的可选入口；不改变现有 render 的平台调用。
use crate::{Layout, Theme};
#[derive(Debug, Clone)]
pub struct PanelConfig {
    pub layout: Layout,

    pub theme: Theme,

    pub scale: f32,

    pub max_width: u32,

    pub max_height: u32,

    pub page: usize,

    pub page_count: usize,
}
impl Default for PanelConfig {
    fn default() -> Self {
        Self {
            layout: Layout::Vertical,
            theme: Theme::light(),
            scale: 1.0,
            max_width: 1600,
            max_height: 900,
            page: 0,
            page_count: 1,
        }
    }
}
