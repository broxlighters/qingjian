//! 可选有界面板入口，沿用现有绘制器；不向共享帧加入平台协议或学习信息。
mod fit;
use super::{Metrics, Renderer};
use crate::{Frame, PanelConfig, Rect, RenderError, RenderTiming, Rendered, RenderedPanel, Shadow};
use std::time::Instant;

impl Renderer {
    pub fn render_panel(
        &mut self,
        source: &Frame,
        config: &PanelConfig,
    ) -> Result<RenderedPanel, RenderError> {
        let started = Instant::now();
        let (width, height) = (config.max_width, config.max_height);
        if !config.scale.is_finite()
            || !(0.5..=4.0).contains(&config.scale)
            || width == 0
            || height == 0
            || width > 8192
            || height > 8192
            || u64::from(width) * u64::from(height) > 16_777_216
        {
            return Err(RenderError::InvalidSize { width, height });
        }
        if source.is_empty() {
            return Ok(RenderedPanel {
                image: Rendered {
                    pixmap: crate::Pixmap::new(1, 1).unwrap(),
                    content_x: 0,
                    content_y: 0,
                    content_width: 0,
                    content_height: 0,
                    scale: config.scale,
                    geometry: Default::default(),
                },
                previous_page: None,
                next_page: None,
                truncated: false,
                timing: RenderTiming::default(),
            });
        }
        let shadow = Shadow::mac_panel();
        let m = Metrics {
            theme: &config.theme,
            scale: config.scale,
        };
        let margin = shadow.margin() * config.scale;
        let available = width as f32 - 2.0 * (margin + m.padding()) - 2.0;
        if available < m.px(16.0) {
            return Err(RenderError::InvalidSize { width, height });
        }
        let mut frame = source.clone();
        frame.rows.truncate(config.theme.max_rows.min(9));
        // 页码仅由明确的分页数据产生，左右箭头无效时仍占位，避免布局跳动。
        frame.footer = (config.page_count > 1).then(|| {
            format!(
                "‹ {}/{} ›",
                config.page.saturating_add(1),
                config.page_count
            )
        });
        for row in &mut frame.rows {
            if row.text.is_empty() {
                row.annotation.clear();
            }
        }
        self.fit_panel(&mut frame, config, available);
        loop {
            let (cw, ch) = self.preferred_size(&frame, config.layout, &m);
            if (cw + margin * 2.0).ceil() <= width as f32
                && (ch + margin * 2.0).ceil() <= height as f32
            {
                break;
            }
            // 只移除整行，原槽位保持不变；连拼音/页码也放不下时交回系统面板。
            if frame.rows.pop().is_none() {
                return Err(RenderError::InvalidSize { width, height });
            }
        }
        if frame
            .highlighted
            .is_some_and(|i| i >= frame.rows.len() || frame.rows[i].text.is_empty())
        {
            frame.highlighted = None;
        }
        let layout_ns = started.elapsed().as_nanos() as u64;
        let raster = Instant::now();
        let mut image = self.render(
            &frame,
            config.layout,
            &config.theme,
            config.scale,
            Some(&shadow),
        )?;
        // 被省略或清理过的片段不能报告为完整展示；只保存索引，不缓存文本。
        image.geometry.annotations.retain(|&(row, segment)| {
            source.rows.get(row).and_then(|r| r.annotation.get(segment))
                == frame.rows.get(row).and_then(|r| r.annotation.get(segment))
        });
        let mut previous_page = None;
        let mut next_page = None;
        if let Some(footer) = image.geometry.footer {
            let arrow = self.measure("‹ ", &m.index_style()).width;
            let last = self.measure(" ›", &m.index_style()).width;
            if config.page > 0 {
                previous_page = Some(Rect {
                    width: arrow,
                    ..footer
                });
            }
            if config.page.saturating_add(1) < config.page_count {
                next_page = Some(Rect {
                    x: footer.x + footer.width - last,
                    width: last,
                    ..footer
                });
            }
        }
        // 页脚是 UI 装饰，不计入内容截断。
        frame.footer = source.footer.clone();
        Ok(RenderedPanel {
            image,
            previous_page,
            next_page,
            truncated: frame != *source,
            timing: RenderTiming {
                layout_ns,
                raster_ns: raster.elapsed().as_nanos() as u64,
            },
        })
    }
}
