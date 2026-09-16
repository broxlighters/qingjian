//! 拥有字形的整形行，可同时供测量和绘制复用。
use cosmic_text::LayoutGlyph;

pub(super) struct ShapedRun {
    pub glyphs: Vec<LayoutGlyph>,

    pub line_y: f32,

    pub width: f32,
}
