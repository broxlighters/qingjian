//! 候选原槽位对应的完整行区域，空槽没有区域。
use super::Rect;
#[derive(Debug, Clone, PartialEq)]
pub struct HitRegion {
    pub row: usize,

    pub rect: Rect,
}
