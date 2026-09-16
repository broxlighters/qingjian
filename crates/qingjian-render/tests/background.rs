//! 缓存不能留下旧主题、尺寸或缩放；结果必须等同独立渲染器。
use qingjian_render::{Frame, RenderConfig, Renderer, Row, ThemeMode};
fn renderer() -> Renderer {
    Renderer::with_fonts(
        [include_bytes!("fonts/NotoSans-Regular.ttf").to_vec()],
        "Noto Sans",
    )
    .unwrap()
}
#[test]
fn reused_background_matches_fresh_renderer_after_theme_size_and_scale_changes() {
    let mut reused = renderer();
    for theme in [ThemeMode::Light, ThemeMode::Dark, ThemeMode::Light] {
        for scale in [1.0, 2.0, 1.25] {
            for text in ["word", "a much wider candidate", "word"] {
                let frame = Frame {
                    rows: vec![Row::new("1", text)],
                    theme,
                    ..Default::default()
                };
                let config = RenderConfig {
                    scale,
                    ..Default::default()
                };
                assert_eq!(
                    reused.render(&frame, config).unwrap(),
                    renderer().render(&frame, config).unwrap()
                );
            }
        }
    }
}
