//! 位图中可交互的矩形；action 为候选原槽位、上一页 -1 或下一页 -2。
#[repr(C)]
pub struct InteractionRegion {
    pub x: f32,

    pub y: f32,

    pub width: f32,

    pub height: f32,

    pub action: i32,
}
