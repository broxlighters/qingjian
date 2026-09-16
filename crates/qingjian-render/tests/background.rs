//! 缓存不能留下旧主题、阴影、尺寸或缩放；结果必须等同独立渲染器。
mod support;
use qingjian_render::{Frame, Layout, Row, Shadow, Theme};
#[test]
fn reused_background_matches_fresh_renderer_after_theme_size_and_scale_changes() {
    let mut reused = support::renderer();
    for theme in [Theme::light(), Theme::dark(), Theme::light()] {
        for scale in [1.0, 2.0, 1.25] {
            for text in ["word", "a much wider candidate", "word"] {
                let frame = Frame {
                    rows: vec![Row::plain(0, text)],
                    ..Default::default()
                };
                for shadow in [None, Some(Shadow::mac_panel())] {
                    let a = reused
                        .render(&frame, Layout::Vertical, &theme, scale, shadow.as_ref())
                        .unwrap();
                    let b = support::renderer()
                        .render(&frame, Layout::Vertical, &theme, scale, shadow.as_ref())
                        .unwrap();
                    assert_eq!(a.pixmap, b.pixmap);
                    assert_eq!(a.geometry, b.geometry);
                }
            }
        }
    }
}
