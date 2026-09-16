//! 屏幕尺寸限制、空帧和非法缩放的回归。
mod support;
use qingjian_render::{Frame, Layout, PanelConfig, Preedit, Row, Tone};
#[test]
fn long_content_is_bounded_without_changing_the_input_frame() {
    let mut renderer = support::renderer();
    let mut word = Row::plain(0, "qingjian ".repeat(200));
    word.annotation = vec![("e\u{301}👩‍💻 ".repeat(200), Tone::Fresh)];
    let frame = Frame {
        preedit: Some(Preedit::plain("abcdefgh".repeat(100).as_str(), 800)),
        rows: vec![word.clone(), Row::plain(1, ""), word],
        highlighted: Some(0),
        ..Default::default()
    };
    let original = frame.clone();
    for layout in [Layout::Vertical, Layout::Horizontal] {
        for scale in [1.0, 1.25, 1.5, 2.0] {
            let config = PanelConfig {
                layout,
                scale,
                max_width: (380.0 * scale) as u32,
                max_height: (220.0 * scale) as u32,
                ..Default::default()
            };
            let result = renderer.render_panel(&frame, &config).unwrap();
            assert!(
                result.truncated
                    && result.image.pixmap.width() <= config.max_width
                    && result.image.pixmap.height() <= config.max_height
            );
            assert!(result.image.geometry.annotations.is_empty());
            assert_eq!(result.image.geometry.hit_test(0, 0), None);
        }
    }
    assert_eq!(frame, original);
}
#[test]
fn invalid_sizes_fail_even_when_empty_and_empty_frames_are_transparent() {
    let mut renderer = support::renderer();
    for config in [
        PanelConfig {
            scale: f32::NAN,
            ..Default::default()
        },
        PanelConfig {
            max_width: 0,
            ..Default::default()
        },
        PanelConfig {
            max_width: u32::MAX,
            ..Default::default()
        },
    ] {
        assert!(renderer.render_panel(&Frame::default(), &config).is_err());
    }
    let result = renderer
        .render_panel(&Frame::default(), &PanelConfig::default())
        .unwrap();
    assert_eq!(result.image.pixmap.data(), &[0, 0, 0, 0]);
    let frame = Frame {
        rows: vec![Row::plain(0, "word")],
        ..Default::default()
    };
    assert!(
        renderer
            .render_panel(
                &frame,
                &PanelConfig {
                    max_height: 20,
                    ..Default::default()
                }
            )
            .is_err()
    );
}
