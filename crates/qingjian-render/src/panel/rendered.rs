//! 有界面板位图、可点页码和展示耗时。
use crate::{Rect, RenderTiming, Rendered};
pub struct RenderedPanel {
    pub image: Rendered,

    pub previous_page: Option<Rect>,

    pub next_page: Option<Rect>,

    pub truncated: bool,

    pub timing: RenderTiming,
}
