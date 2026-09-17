//! 一次渲染的文字、主题与装饰单位换算。
use super::{CLOUD_GAP, CLOUD_SIZE};
use crate::text::TextStyle;
use crate::{Color, FontSpec, Theme, Tone};

/// 一次渲染期间的上下文：主题按倍数换算后的像素值。
pub(super) struct Metrics<'a> {
    pub(super) theme: &'a Theme,
    pub(super) scale: f32,
}

impl Metrics<'_> {
    pub(super) fn px(&self, points: f32) -> f32 {
        points * self.scale
    }

    pub(super) fn decoration_px(&self, points: f32) -> f32 {
        self.px(points * self.theme.decoration_scale)
    }

    pub(super) fn padding(&self) -> f32 {
        self.px(self.theme.padding)
    }

    pub(super) fn row_padding(&self) -> f32 {
        self.px(self.theme.row_padding)
    }

    pub(super) fn column_gap(&self) -> f32 {
        self.px(self.theme.column_gap)
    }

    pub(super) fn corner_radius(&self) -> f32 {
        self.px(self.theme.corner_radius)
    }

    pub(super) fn style(&self, font: FontSpec, color: Color) -> TextStyle {
        TextStyle::new(
            font.scaled(self.scale),
            font.size,
            color,
            self.theme.text_gamma,
        )
    }

    pub(super) fn text_style(&self) -> TextStyle {
        self.style(self.theme.text_font, self.theme.colors.text)
    }

    pub(super) fn annotation_style(&self, color: Color) -> TextStyle {
        self.style(self.theme.annotation_font, color)
    }

    /// 序号预算随字号变化，避免 UI/文字放大后仍按原始 24 像素裁字。
    pub(super) fn index_budget(&self) -> f32 {
        self.px(24.0 * self.theme.index_font.size / 11.0)
    }

    pub(super) fn index_style(&self) -> TextStyle {
        self.style(self.theme.index_font, self.theme.colors.index)
    }

    pub(super) fn tone_color(&self, tone: Tone) -> Color {
        match tone {
            Tone::Gloss => self.theme.colors.gloss,
            Tone::Fresh => self.theme.colors.fresh,
            Tone::Faint => self.theme.colors.pos,
        }
    }

    /// 小字相对候选词往下挪多少，让两者底部对齐。
    pub(super) fn small_offset(&self, text_height: f32) -> f32 {
        (text_height - self.px(self.theme.annotation_font.line_height)).max(0.0)
    }

    /// 云朵图标占的宽度（含后面的间距）。
    pub(super) fn cloud_width(&self) -> f32 {
        self.decoration_px(CLOUD_SIZE + CLOUD_GAP)
    }
}
