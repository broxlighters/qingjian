//! 复用整形结果后，高亮、颜色、分页与缩放不能沿用旧几何或像素。
mod support;
use qingjian_render::{Frame, Layout, PanelConfig, Preedit, Row, Theme, Tone};

#[test]
fn cached_text_matches_fresh_after_styles_and_private_cleanup() {
    let mut renderer = support::renderer();
    let mut row = Row::plain(0, "青简 hello 👩‍💻");
    row.annotation = vec![("日本語 にほんご".into(), Tone::Fresh)];
    let mut frame = Frame {
        preedit: Some(Preedit::plain("qingjian", 8)),
        rows: vec![row, Row::plain(1, "other")],
        highlighted: Some(0),
        ..Default::default()
    };
    for scale in [1.0, 1.25, 1.5, 2.0, 1.0] {
        for (page, theme) in [Theme::light(), Theme::dark()].into_iter().enumerate() {
            frame.highlighted = Some(page);
            frame.rows[0].annotation[0].1 = if page == 0 { Tone::Fresh } else { Tone::Gloss };
            let config = PanelConfig {
                layout: Layout::Vertical,
                scale,
                theme,
                page,
                page_count: 2,
                ..Default::default()
            };
            let actual = renderer.render_panel(&frame, &config).unwrap();
            let expected = support::renderer().render_panel(&frame, &config).unwrap();
            assert_eq!(actual.image.pixmap, expected.image.pixmap);
            assert_eq!(actual.image.geometry, expected.image.geometry);
            assert_eq!(actual.previous_page, expected.previous_page);
            assert_eq!(actual.next_page, expected.next_page);
            renderer.clear_text_cache();
            let cleared = renderer.render_panel(&frame, &config).unwrap();
            assert_eq!(cleared.image.pixmap, expected.image.pixmap);
        }
    }
}
