//! 绘制过程中记录的几何，与实际文字和高亮使用同一坐标。
mod hit;
mod rect;
pub use hit::HitRegion;
pub use rect::Rect;
#[derive(Debug, Clone, Default, PartialEq)]
pub struct RenderGeometry {
    pub candidates: Vec<HitRegion>,

    /// 已绘制的 annotation 片段下标（原候选槽位、片段位置）。
    pub annotations: Vec<(usize, usize)>,

    pub footer: Option<Rect>,
}
impl RenderGeometry {
    pub fn hit_test(&self, x: u32, y: u32) -> Option<usize> {
        self.candidates
            .iter()
            .find(|hit| hit.rect.contains(x, y))
            .map(|hit| hit.row)
    }
}
