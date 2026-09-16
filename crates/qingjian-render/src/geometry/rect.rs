//! 位图中的半开矩形；只接受完整落在矩形内的像素。
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Rect {
    pub x: f32,

    pub y: f32,

    pub width: f32,

    pub height: f32,
}
impl Rect {
    pub fn contains(self, x: u32, y: u32) -> bool {
        let (x, y) = (x as f32, y as f32);
        x >= self.x && y >= self.y && x < self.x + self.width && y < self.y + self.height
    }
}
