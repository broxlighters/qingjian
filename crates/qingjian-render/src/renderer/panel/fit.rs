//! 按现有画笔测量后做文本簇省略；只修改展示副本，绝不改提交文本。
use super::super::{CARET_WIDTH, HIGHLIGHT_INSET, INDEX_GAP, Metrics, Renderer, SENTENCE_GAP};
use crate::text::TextStyle;
use crate::{Frame, Layout, PanelConfig, Tone};
use unicode_segmentation::UnicodeSegmentation;

impl Renderer {
    pub(super) fn fit_panel(&mut self, frame: &mut Frame, config: &PanelConfig, available: f32) {
        let m = Metrics {
            theme: &config.theme,
            scale: config.scale,
        };
        let annotation_style = m.annotation_style(m.theme.colors.gloss);
        let trailing = frame.trailing().is_some();
        let top_budget = if trailing && frame.preedit.is_some() {
            available * 0.55
        } else {
            available
        };
        if let Some(preedit) = &mut frame.preedit {
            let mut remaining = top_budget - m.decoration_px(CARET_WIDTH);
            for segment in &mut preedit.segments {
                segment.text = self.fit_text(&segment.text, &annotation_style, remaining);
                remaining -= self.measure(&segment.text, &annotation_style).width;
            }
            preedit.cursor = preedit.cursor.min(preedit.text().chars().count());
        }
        let trailing_budget = if frame.preedit.is_some() {
            available - top_budget - m.decoration_px(SENTENCE_GAP)
        } else {
            available
        };
        if let Some(status) = &mut frame.status {
            *status = self.fit_text(status, &annotation_style, trailing_budget);
        } else if let Some(sentence) = &mut frame.sentence {
            *sentence = self.fit_text(
                sentence,
                &annotation_style,
                trailing_budget - m.cloud_width(),
            );
        }
        if let Some(footer) = &mut frame.footer {
            // 不截断页码，放不下时由最终尺寸检查触发回退。
            if self.measure(footer, &m.index_style()).width > available {
                return;
            }
        }
        match config.layout {
            Layout::Vertical => {
                for row in &mut frame.rows {
                    row.index = self.fit_text(&row.index, &m.index_style(), m.index_budget());
                }
                let index = self.columns(&frame.rows, &m).index_width;
                let word_budget = (available - index - m.column_gap())
                    * if frame.rows.iter().any(|r| !r.annotation.is_empty()) {
                        0.5
                    } else {
                        1.0
                    };
                for row in &mut frame.rows {
                    row.text = self.fit_text(
                        &row.text,
                        &m.text_style(),
                        word_budget - if row.cloud { m.cloud_width() } else { 0.0 },
                    );
                }
                let columns = self.columns(&frame.rows, &m);
                let budget =
                    available - columns.index_width - columns.text_width - m.column_gap() * 2.0;
                for row in &mut frame.rows {
                    self.fit_annotation(&mut row.annotation, &annotation_style, budget);
                }
            }
            Layout::Horizontal => {
                let footer_width = frame.footer.as_ref().map_or(0.0, |s| {
                    self.measure(s, &m.index_style()).width + m.column_gap()
                });
                let count = frame.rows.len().max(1) as f32;
                let budget = (available
                    - footer_width
                    - m.decoration_px(HIGHLIGHT_INSET) * 2.0
                    - m.column_gap() * (count - 1.0))
                    / count;
                for row in &mut frame.rows {
                    row.index = self.fit_text(&row.index, &m.index_style(), m.index_budget());
                    let word_budget = budget
                        - self.measure(&row.index, &m.index_style()).width
                        - m.decoration_px(INDEX_GAP)
                        - if row.cloud { m.cloud_width() } else { 0.0 };
                    row.text = self.fit_text(&row.text, &m.text_style(), word_budget);
                    self.fit_annotation(
                        &mut row.annotation,
                        &annotation_style,
                        available - m.decoration_px(HIGHLIGHT_INSET),
                    );
                }
            }
        }
    }

    fn fit_annotation(
        &mut self,
        segments: &mut [(String, Tone)],
        style: &TextStyle,
        mut width: f32,
    ) {
        for (text, _) in segments {
            *text = self.fit_text(text, style, width);
            width -= self.measure(text, style).width;
        }
    }

    fn fit_text(&mut self, text: &str, style: &TextStyle, width: f32) -> String {
        if text.is_empty() || width <= 0.0 {
            return String::new();
        }
        // 单行语义：控制字符不允许在 buffer 中引入额外行或制表跳跃。
        let clean: String = text
            .chars()
            .map(|c| if c.is_control() { ' ' } else { c })
            .collect();
        if self.measure(&clean, style).width <= width {
            return clean;
        }
        if self.measure("…", style).width > width {
            return String::new();
        }
        let ends: Vec<usize> = clean
            .grapheme_indices(true)
            .map(|(i, s)| i + s.len())
            .collect();
        let (mut low, mut high) = (0, ends.len());
        while low < high {
            let middle = (low + high).div_ceil(2);
            let candidate = format!("{}…", &clean[..ends[middle - 1]]);
            if self.measure(&candidate, style).width <= width {
                low = middle;
            } else {
                high = middle - 1;
            }
        }
        format!("{}…", &clean[..if low == 0 { 0 } else { ends[low - 1] }])
    }
}
