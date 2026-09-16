//! 一个义项必须完整画出其全部片段才回报曝光。
use std::ops::Range;
pub(crate) struct SenseRange {
    pub row: usize,

    pub sense: usize,

    pub segments: Range<usize>,
}
