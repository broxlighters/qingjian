//! 文字整形缓存：最多 512 段、2 MiB，光学字号变更和隐私清理时作废。
use super::{shaped::ShapedText, style::TextStyle};
use std::{collections::VecDeque, sync::Arc};

type Key = (String, u32, u32, u32);
const MAX_BYTES: usize = 2 * 1024 * 1024;

#[derive(Default)]
pub(super) struct ShapeCache {
    entries: VecDeque<(Key, Arc<ShapedText>, usize)>,

    bytes: usize,
}

impl ShapeCache {
    pub fn get(&self, text: &str, style: &TextStyle) -> Option<Arc<ShapedText>> {
        self.entries
            .iter()
            .rev()
            .find(|((stored, size, height, points), _, _)| {
                stored == text
                    && *size == style.size.to_bits()
                    && *height == style.line_height.to_bits()
                    && *points == style.points.to_bits()
            })
            .map(|(_, value, _)| Arc::clone(value))
    }

    pub fn insert(&mut self, text: &str, style: &TextStyle, value: Arc<ShapedText>) {
        let bytes = text.len() + value.bytes();
        if bytes > MAX_BYTES {
            return;
        }
        while self.entries.len() >= 512 || self.bytes + bytes > MAX_BYTES {
            if let Some((_, _, old)) = self.entries.pop_front() {
                self.bytes -= old;
            }
        }
        self.entries.push_back((
            (
                text.to_owned(),
                style.size.to_bits(),
                style.line_height.to_bits(),
                style.points.to_bits(),
            ),
            value,
            bytes,
        ));
        self.bytes += bytes;
    }

    pub fn clear(&mut self) {
        self.entries.clear();
        self.bytes = 0;
    }
}
