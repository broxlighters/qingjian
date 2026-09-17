//! Linux 尺寸只调整主题副本：文字/行高乘 u×t，其余逻辑尺寸乘 u。
use qingjian_render::Theme;

pub(super) fn scaled_theme(mut theme: Theme, ui: f32, text: f32) -> Option<Theme> {
    if !ui.is_finite()
        || !(0.75..=2.0).contains(&ui)
        || !text.is_finite()
        || !(0.5..=3.0).contains(&text)
    {
        return None;
    }
    for font in [
        &mut theme.text_font,
        &mut theme.annotation_font,
        &mut theme.index_font,
    ] {
        font.size *= ui * text;
        font.line_height *= ui * text;
    }
    theme.padding *= ui;
    theme.row_padding *= ui;
    theme.column_gap *= ui;
    theme.corner_radius *= ui;
    theme.decoration_scale *= ui;
    Some(theme)
}
