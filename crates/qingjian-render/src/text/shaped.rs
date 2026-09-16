//! 不带颜色与删除线的整形结果；样式变化只影响绘制。
use super::run::ShapedRun;

pub(super) struct ShapedText {
    pub runs: Vec<ShapedRun>,

    pub width: f32,
}

impl ShapedText {
    pub fn bytes(&self) -> usize {
        self.runs.capacity() * std::mem::size_of::<ShapedRun>()
            + self
                .runs
                .iter()
                .map(|run| run.glyphs.capacity() * std::mem::size_of::<cosmic_text::LayoutGlyph>())
                .sum::<usize>()
    }
}
