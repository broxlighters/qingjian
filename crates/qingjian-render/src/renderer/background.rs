//! 背景与阴影只缓存像素，不保存输入文本；最多 8 张且总计不超过 8 MiB。
use crate::canvas::Canvas;
use crate::{Color, RenderError, Shadow};
use std::collections::VecDeque;
use tiny_skia::Pixmap;

type Key = (u32, u32, f32, f32, f32, f32, Color, Option<Shadow>);
#[derive(Default)]
pub(super) struct BackgroundCache {
    entries: VecDeque<(Key, Pixmap)>,

    bytes: usize,
}
impl BackgroundCache {
    pub(super) fn frame(
        &mut self,
        content: (f32, f32),
        margin: f32,
        radius: f32,
        scale: f32,
        color: Color,
        shadow: Option<&Shadow>,
    ) -> Result<Canvas, RenderError> {
        let (cw, ch) = content;
        let (width, height) = (
            (cw + 2.0 * margin).ceil() as u32,
            (ch + 2.0 * margin).ceil() as u32,
        );
        let key = (width, height, cw, ch, radius, scale, color, shadow.copied());
        if let Some((_, pixels)) = self.entries.iter().find(|(stored, _)| *stored == key) {
            return Ok(Canvas::from_pixmap(pixels.clone()));
        }
        let mut canvas = Canvas::new(width, height)?;
        if let Some(shadow) = shadow
            && let Some(rect) = tiny_skia::Rect::from_xywh(margin, margin, cw, ch)
        {
            shadow.paint(&mut canvas, rect, radius, scale);
        }
        canvas.fill_round_rect(margin, margin, cw, ch, radius, color);
        let pixels = canvas.into_pixmap();
        let size = pixels.data().len();
        if size <= 8 * 1024 * 1024 {
            while self.entries.len() >= 8 || self.bytes + size > 8 * 1024 * 1024 {
                if let Some((_, old)) = self.entries.pop_front() {
                    self.bytes -= old.data().len();
                }
            }
            self.entries.push_back((key, pixels.clone()));
            self.bytes += size;
        }
        Ok(Canvas::from_pixmap(pixels))
    }
}
