//! 缓存背景与阴影，避免每键重复多层半透明填充；总缓存最多 8 MiB。
use super::scene::Scene;
use crate::{Rect, RenderConfig, RenderError};
use std::collections::VecDeque;
use tiny_skia::{FillRule, Paint, PathBuilder, Pixmap, Transform};

type Key = (u32, u32, u32, u32, [u8; 4]);
#[derive(Default)]
pub(super) struct BackgroundCache {
    /// 最近 8 个尺寸/缩放/背景颜色，单图超过 8 MiB 不驻留。
    entries: VecDeque<(Key, Pixmap)>,

    /// 已缓存位图的实际字节总数。
    bytes: usize,
}
impl BackgroundCache {
    pub fn frame(&mut self, scene: &Scene, c: RenderConfig) -> Result<Pixmap, RenderError> {
        let key = (
            scene.width,
            scene.height,
            scene.shadow,
            c.scale.to_bits(),
            scene.theme.background,
        );
        if let Some((_, pixmap)) = self.entries.iter().find(|(stored, _)| *stored == key) {
            return Ok(pixmap.clone());
        }
        let mut pixmap = Pixmap::new(scene.width, scene.height).ok_or(RenderError::Allocation)?;
        if scene.width > scene.shadow * 2 && scene.height > scene.shadow * 2 {
            // 由外到内的半透明同心圆角轮廓形成柔和阴影，外围保留透明点击空区。
            for spread in (1..=scene.shadow).rev() {
                let d = scene.shadow - spread;
                rounded(
                    &mut pixmap,
                    Rect {
                        x: d,
                        y: d + 1,
                        width: scene.width - 2 * d,
                        height: scene.height - 2 * d - 1,
                    },
                    8.0 * c.scale + spread as f32,
                    [0, 0, 0, 5],
                );
            }
            rounded(
                &mut pixmap,
                Rect {
                    x: scene.shadow,
                    y: scene.shadow,
                    width: scene.width - 2 * scene.shadow,
                    height: scene.height - 2 * scene.shadow,
                },
                8.0 * c.scale,
                scene.theme.background,
            );
        }

        let length = pixmap.data().len();
        if length <= 8 * 1024 * 1024 {
            while self.entries.len() >= 8 || self.bytes + length > 8 * 1024 * 1024 {
                if let Some((_, previous)) = self.entries.pop_front() {
                    self.bytes -= previous.data().len();
                }
            }
            self.entries.push_back((key, pixmap.clone()));
            self.bytes += length;
        }
        Ok(pixmap)
    }
}

pub(super) fn rounded(pixmap: &mut Pixmap, rect: Rect, radius: f32, color: [u8; 4]) {
    if rect.width == 0 || rect.height == 0 {
        return;
    }
    let x = rect.x as f32;
    let y = rect.y as f32;
    let w = rect.width as f32;
    let h = rect.height as f32;
    let r = radius.min(w / 2.0).min(h / 2.0);
    let mut path = PathBuilder::new();
    path.move_to(x + r, y);
    path.line_to(x + w - r, y);
    path.quad_to(x + w, y, x + w, y + r);
    path.line_to(x + w, y + h - r);
    path.quad_to(x + w, y + h, x + w - r, y + h);
    path.line_to(x + r, y + h);
    path.quad_to(x, y + h, x, y + h - r);
    path.line_to(x, y + r);
    path.quad_to(x, y, x + r, y);
    path.close();
    if let Some(path) = path.finish() {
        let mut paint = Paint::default();
        paint.set_color_rgba8(color[0], color[1], color[2], color[3]);
        pixmap.fill_path(
            &path,
            &paint,
            FillRule::Winding,
            Transform::identity(),
            None,
        );
    }
}
