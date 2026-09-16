//! 两种布局的页码和候选命中区域必须相互分离。
mod support;
use qingjian_render::{Frame, Layout, PanelConfig, Row};
#[test]
fn page_regions_are_enabled_only_when_the_page_exists() {
    let mut renderer = support::renderer();
    let frame = Frame {
        rows: vec![Row::plain(0, "hello")],
        ..Default::default()
    };
    for layout in [Layout::Vertical, Layout::Horizontal] {
        for page in 0..3 {
            let config = PanelConfig {
                layout,
                page,
                page_count: 3,
                ..Default::default()
            };
            let rendered = renderer.render_panel(&frame, &config).unwrap();
            assert_eq!(rendered.previous_page.is_some(), page > 0);
            assert_eq!(rendered.next_page.is_some(), page < 2);
            for rect in [rendered.previous_page, rendered.next_page]
                .into_iter()
                .flatten()
            {
                assert_eq!(
                    rendered
                        .image
                        .geometry
                        .hit_test(rect.x.ceil() as u32, rect.y.ceil() as u32),
                    None
                );
                assert!(rect.x + rect.width <= rendered.image.pixmap.width() as f32);
                assert!(rect.y + rect.height <= rendered.image.pixmap.height() as f32);
            }
        }
    }
}
