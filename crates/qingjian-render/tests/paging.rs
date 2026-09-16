//! 页码点击与候选命中互斥，首尾页不生成不可用按钮。
use qingjian_render::{Frame, LayoutMode, RenderConfig, Renderer, Row};
#[test]
fn page_arrows_match_visible_footer_and_page_bounds() {
    let mut renderer = Renderer::with_fonts(
        [include_bytes!("fonts/NotoSans-Regular.ttf").to_vec()],
        "Noto Sans",
    )
    .unwrap();
    for layout in [LayoutMode::Vertical, LayoutMode::Horizontal] {
        for page in 0..3 {
            let frame = Frame {
                rows: vec![Row::new("1", "word")],
                page,
                page_count: 3,
                layout,
                ..Default::default()
            };
            let rendered = renderer.render(&frame, RenderConfig::default()).unwrap();
            assert_eq!(rendered.previous_page.is_some(), page > 0);
            assert_eq!(rendered.next_page.is_some(), page < 2);
            for rect in [rendered.previous_page, rendered.next_page]
                .into_iter()
                .flatten()
            {
                assert_eq!(rendered.hit_test(rect.x, rect.y), None);
                assert!(rect.x + rect.width <= rendered.width);
                assert!(rect.y + rect.height <= rendered.height);
            }
        }
    }
}
