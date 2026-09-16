//! 渲染器只报告完整的片段位置，业务义项曝光由 FFI 适配层换算。
mod support;
use qingjian_render::{Frame, Layout, PanelConfig, Row, Tone};
fn row(index: usize) -> Row {
    let mut row = Row::plain(index, "word");
    row.annotation = vec![
        ("hello".into(), Tone::Fresh),
        (" · ".into(), Tone::Faint),
        ("world".into(), Tone::Gloss),
    ];
    row
}
#[test]
fn only_complete_visible_segments_are_reported() {
    let mut renderer = support::renderer();
    let mut frame = Frame {
        rows: vec![row(0), Row::plain(1, ""), row(2)],
        highlighted: Some(2),
        ..Default::default()
    };
    let mut config = PanelConfig::default();
    let vertical = renderer.render_panel(&frame, &config).unwrap();
    assert_eq!(
        vertical.image.geometry.annotations,
        vec![(0, 0), (0, 1), (0, 2), (2, 0), (2, 1), (2, 2)]
    );
    assert_eq!(
        vertical
            .image
            .geometry
            .candidates
            .iter()
            .map(|hit| hit.row)
            .collect::<Vec<_>>(),
        vec![0, 2]
    );
    config.layout = Layout::Horizontal;
    let horizontal = renderer.render_panel(&frame, &config).unwrap();
    assert_eq!(
        horizontal.image.geometry.annotations,
        vec![(2, 0), (2, 1), (2, 2)]
    );
    frame.rows[2].annotation[2].0 = "extremely long ".repeat(100);
    config.max_width = 340;
    let clipped = renderer.render_panel(&frame, &config).unwrap();
    assert_eq!(clipped.image.geometry.annotations, vec![(2, 0), (2, 1)]);
    assert!(clipped.truncated);
    frame.highlighted = Some(1);
    assert!(
        renderer
            .render_panel(&frame, &config)
            .unwrap()
            .image
            .geometry
            .annotations
            .is_empty()
    );
}
