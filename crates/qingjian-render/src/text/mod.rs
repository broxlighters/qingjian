//! 文字测绘：cosmic-text 整形 + swash 栅格，单行、像素坐标；普通字形走覆盖率遮罩，彩色 emoji 走 RGBA 位图。

mod cache;
mod run;
mod shaped;
mod size;
mod style;

use cache::ShapeCache;
use run::ShapedRun;
use shaped::ShapedText;
use std::collections::HashMap;
use std::sync::Arc;

use cosmic_text::fontdb::ID;
use cosmic_text::{Attrs, Buffer, FontSystem, Metrics, Shaping, SwashCache, SwashContent};

use crate::canvas::Canvas;
use crate::fonts::{FontLibrary, Trak, UI_FAMILY};

pub(crate) use size::TextSize;
pub(crate) use style::TextStyle;

pub(crate) struct TextPainter {
    /// 字体库与回退链。
    font_system: FontSystem,

    /// 字形位图缓存（按字体、字号、亚像素位移）。
    cache: SwashCache,

    /// 复用的单行缓冲。
    buffer: Buffer,

    shapes: ShapeCache,

    /// 光学字号不包含在 cosmic-text 字形键中，变更时同时清理栅格缓存。
    optical_size: Option<f32>,

    raster_bytes: usize,

    /// 每张字体的字距表（没有的记 `None`），按字形所用字体各查各的，与 CoreText 一致。
    tracking: HashMap<ID, Option<Trak>>,

    /// 覆盖率 gamma 查找表，按 gamma 值缓存。
    gamma_tables: HashMap<u32, Box<[u8; 256]>>,
}

impl TextPainter {
    pub(crate) fn new(library: FontLibrary) -> Self {
        let mut font_system = library.into_font_system();
        let buffer = Buffer::new(&mut font_system, Metrics::new(16.0, 19.0));
        Self {
            font_system,
            cache: SwashCache::new(),
            buffer,
            shapes: ShapeCache::default(),
            optical_size: None,
            raster_bytes: 0,
            tracking: HashMap::new(),
            gamma_tables: HashMap::new(),
        }
    }

    /// 光学字号（点）：SF 这类带 `opsz` 轴的字体在小字号用文本视觉尺寸，CoreText 对系统字体自动做，这里要显式给。
    /// 现在是整个画笔一个值（cosmic-text 的字体实例缓存没按它分键），候选窗几种字号都在 20 pt 以下，落到同一档。
    pub(crate) fn set_optical_size(&mut self, points: Option<f32>) {
        if self.optical_size != points {
            self.clear_text_cache();
            self.cache = SwashCache::new();
            self.raster_bytes = 0;
            self.optical_size = points;
        }
        self.font_system.set_optical_size(points);
    }

    /// 一段文字的宽高（像素）。高度就是行高。
    pub(crate) fn measure(&mut self, text: &str, style: &TextStyle) -> TextSize {
        let shaped = self.shape(text, style);
        TextSize {
            width: shaped.width,
            height: style.line_height,
        }
    }

    /// 把文字画到 `(x, y)`，`y` 是行框顶边。返回宽度。
    pub(crate) fn draw(
        &mut self,
        canvas: &mut Canvas,
        text: &str,
        style: &TextStyle,
        x: f32,
        y: f32,
    ) -> f32 {
        let shaped = self.shape(text, style);
        let mut strike: Option<(f32, f32)> = None;
        let gamma = gamma_table(&mut self.gamma_tables, style.gamma);
        let colors = std::array::from_fn(|coverage| style.color.premultiplied(gamma[coverage]));
        for run in &shaped.runs {
            let baseline = y + run.line_y;
            // 每个字形画完把它那份字距累加到后面所有字形的 x 上
            let mut tracked = 0.0_f32;
            for glyph in &run.glyphs {
                let physical = glyph.physical((x + tracked, y), 1.0);
                tracked += tracking_px(&self.font_system, &mut self.tracking, glyph.font_id, style);
                if self.cache.image_cache.len() >= 4096 || self.raster_bytes >= 16 * 1024 * 1024 {
                    self.cache.image_cache.clear();
                    self.raster_bytes = 0;
                }
                let cached = self.cache.image_cache.contains_key(&physical.cache_key);
                let Some(image) = self
                    .cache
                    .get_image(&mut self.font_system, physical.cache_key)
                else {
                    continue;
                };
                if !cached {
                    self.raster_bytes += image.data.len();
                }
                let gx = physical.x + image.placement.left;
                let gy = run.line_y.round() as i32 + physical.y - image.placement.top;
                let (w, h) = (image.placement.width, image.placement.height);
                match image.content {
                    SwashContent::Mask => {
                        canvas.blend_mask_colors(gx, gy, w, h, &image.data, &colors);
                    }
                    SwashContent::Color => canvas.blend_rgba(gx, gy, w, h, &image.data),
                    // 没有申请亚像素格式，不会出现
                    SwashContent::SubpixelMask => {}
                }
            }
            if style.strike {
                strike = Some((baseline, run.width));
            }
        }
        if let Some((baseline, line_w)) = strike {
            // 删除线穿过小写字母中部
            let thickness = (style.size / 14.0).max(1.0);
            let line_y = baseline - style.size * 0.3;
            canvas.fill_rect(x, line_y, line_w, thickness, style.color);
        }
        shaped.width
    }

    /// 每个字形用的字族名（相邻相同的合并），拿来核对中日字形与 emoji 回退到了哪家字体。
    pub(crate) fn trace_families(&mut self, text: &str, style: &TextStyle) -> Vec<String> {
        let shaped = self.shape(text, style);
        let mut names: Vec<String> = Vec::new();
        for run in &shaped.runs {
            for glyph in &run.glyphs {
                let name = self
                    .font_system
                    .db()
                    .face(glyph.font_id)
                    .and_then(|face| face.families.first().map(|(n, _)| n.clone()))
                    .unwrap_or_else(|| "?".to_owned());
                if names.last() != Some(&name) {
                    names.push(name);
                }
            }
        }
        names
    }

    pub(crate) fn clear_text_cache(&mut self) {
        self.shapes.clear();
        self.buffer = Buffer::new(&mut self.font_system, Metrics::new(16.0, 19.0));
    }

    fn shape(&mut self, text: &str, style: &TextStyle) -> Arc<ShapedText> {
        if let Some(cached) = self.shapes.get(text, style) {
            return cached;
        }
        let attrs = Attrs::new().family(UI_FAMILY);
        self.buffer
            .set_metrics(Metrics::new(style.size, style.line_height));
        self.buffer.set_size(None, None);
        self.buffer.set_text(text, &attrs, Shaping::Advanced, None);
        self.buffer.shape_until_scroll(&mut self.font_system, false);
        let mut shaped = ShapedText {
            runs: Vec::new(),
            width: 0.0,
        };
        for run in self.buffer.layout_runs() {
            let tracked: f32 = run
                .glyphs
                .iter()
                .map(|glyph| {
                    tracking_px(&self.font_system, &mut self.tracking, glyph.font_id, style)
                })
                .sum();
            let width = run.line_w + tracked;
            shaped.width = shaped.width.max(width);
            shaped.runs.push(ShapedRun {
                glyphs: run.glyphs.to_vec(),
                line_y: run.line_y,
                width,
            });
        }
        let shaped = Arc::new(shaped);
        self.shapes.insert(text, style, Arc::clone(&shaped));
        shaped
    }
}

/// 某张字体在这个字号下每个字形要加的间距（像素）；第一次用到时解析它的 `trak` 表。
fn tracking_px(
    font_system: &FontSystem,
    cache: &mut HashMap<ID, Option<Trak>>,
    font_id: ID,
    style: &TextStyle,
) -> f32 {
    let trak = cache.entry(font_id).or_insert_with(|| {
        font_system
            .db()
            .with_face_data(font_id, Trak::parse)
            .flatten()
    });
    trak.as_ref()
        .map_or(0.0, |trak| trak.tracking_em(style.points) * style.size)
}

/// 覆盖率 → gamma 校正后的覆盖率。
fn gamma_table(cache: &mut HashMap<u32, Box<[u8; 256]>>, gamma: f32) -> &[u8; 256] {
    if cache.len() >= 16 && !cache.contains_key(&gamma.to_bits()) {
        cache.clear();
    }
    cache.entry(gamma.to_bits()).or_insert_with(|| {
        let mut table = [0u8; 256];
        for (i, out) in table.iter_mut().enumerate() {
            *out = ((i as f32 / 255.0).powf(gamma) * 255.0).round() as u8;
        }
        Box::new(table)
    })
}
