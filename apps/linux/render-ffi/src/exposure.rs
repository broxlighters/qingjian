//! 已完整呈现的候选义项；窗口提交后才可回报 Server。
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Exposure {
    /// 页内原始候选槽位。
    pub row: usize,

    /// Translation 内的原始义项编号。
    pub sense: usize,
}
