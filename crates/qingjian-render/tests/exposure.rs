//! 曝光只包含实际完整可见的义项，不把横排其他候选或截断内容算进去。
use qingjian_render::{Exposure, Frame, LayoutMode, RenderConfig, Renderer, Row, Segment, Tone};
fn row(index: &str) -> Row {
    let mut row = Row::new(index, "word");
    row.annotation = vec![
        Segment {
            text: "hello".into(),
            tone: Tone::Fresh,
            sense: Some(0),
        },
        Segment::new(" · ", Tone::Faint),
        Segment {
            text: "world".into(),
            tone: Tone::Gloss,
            sense: Some(1),
        },
    ];
    row
}
#[test]
fn only_complete_visible_senses_are_reported() {
    let mut renderer = Renderer::with_fonts(
        [include_bytes!("fonts/NotoSans-Regular.ttf").to_vec()],
        "Noto Sans",
    )
    .unwrap();
    let mut frame = Frame {
        rows: vec![row("1"), Row::new("2", ""), row("3")],
        highlighted: Some(2),
        ..Default::default()
    };
    let vertical = renderer.render(&frame, RenderConfig::default()).unwrap();
    assert_eq!(
        vertical.exposures,
        vec![
            Exposure { row: 0, sense: 0 },
            Exposure { row: 0, sense: 1 },
            Exposure { row: 2, sense: 0 },
            Exposure { row: 2, sense: 1 }
        ]
    );
    frame.layout = LayoutMode::Horizontal;
    let horizontal = renderer.render(&frame, RenderConfig::default()).unwrap();
    assert_eq!(
        horizontal.exposures,
        vec![Exposure { row: 2, sense: 0 }, Exposure { row: 2, sense: 1 }]
    );
    frame.rows[2].annotation[2].text = "extremely long ".repeat(100);
    let clipped = renderer
        .render(
            &frame,
            RenderConfig {
                max_width: 300,
                ..Default::default()
            },
        )
        .unwrap();
    assert_eq!(clipped.exposures, vec![Exposure { row: 2, sense: 0 }]);
    frame.highlighted = Some(1);
    assert!(
        renderer
            .render(&frame, RenderConfig::default())
            .unwrap()
            .exposures
            .is_empty()
    );
}
