//! 固定许可字体验证真实重排、阴影比例、命中与旧 ABI 兼容。
use qingjian_core::{Candidate, CandidateKind, CandidateList};
use qingjian_platform::{LayoutMode, protocol::Frame};
use qingjian_render::{FontLibrary, Renderer};
use qingjian_render_ffi::{
    ABI_VERSION, ImageInfo, SIZE_ABI_VERSION, qj_renderer_destroy, qj_renderer_render,
    qj_renderer_render_sized, qj_result_destroy, qj_result_hit, qj_result_image,
};

#[test]
fn user_size_rerenders_geometry_shadow_and_preserves_old_entry() {
    let renderer =
        Renderer::new(
            FontLibrary::from_fonts(
                [include_bytes!(
                    "../../../../crates/qingjian-render/tests/fonts/NotoSans-Regular.ttf"
                )
                .to_vec()],
                "Noto Sans",
                "en-US",
            )
            .unwrap(),
        );
    let handle = Box::into_raw(Box::new(renderer));
    let frame = Frame {
        layout: LayoutMode::Vertical,
        candidates: CandidateList {
            items: vec![Candidate {
                text: "Hello".into(),
                kind: CandidateKind::Cloud,
                syllables: vec![],
                reading: None,
                translation: None,
            }],
        },
        ..Frame::default()
    };
    let json = serde_json::to_vec(&frame).unwrap();
    unsafe {
        let legacy = qj_renderer_render(
            handle,
            ABI_VERSION,
            json.as_ptr(),
            json.len(),
            1.0,
            1600,
            900,
            0,
        );
        let render = |ui, text, version| {
            qj_renderer_render_sized(
                handle,
                ABI_VERSION,
                version,
                json.as_ptr(),
                json.len(),
                1.0,
                ui,
                text,
                1600,
                900,
                0,
            )
        };
        let base = render(1.0, 1.0, SIZE_ABI_VERSION);
        let large = render(2.0, 1.0, SIZE_ABI_VERSION);
        let text = render(1.0, 1.25, SIZE_ABI_VERSION);
        let mut infos = [
            ImageInfo::default(),
            ImageInfo::default(),
            ImageInfo::default(),
            ImageInfo::default(),
        ];
        for (result, info) in [legacy, base, large, text]
            .into_iter()
            .zip(infos.iter_mut())
        {
            assert!(!result.is_null());
            assert!(qj_result_image(result, info));
        }
        let [old, small, big, enlarged] = &infos;
        assert_eq!((old.width, old.height), (small.width, small.height));
        assert_eq!(
            std::slice::from_raw_parts(old.pixels, old.length as usize),
            std::slice::from_raw_parts(small.pixels, small.length as usize)
        );
        assert!(big.width.abs_diff(small.width * 2) <= 2);
        assert!(big.height.abs_diff(small.height * 2) <= 2);
        assert!(enlarged.height > small.height && enlarged.height < big.height);
        let first_hit = |result, info: &ImageInfo| {
            (0..info.height)
                .flat_map(|y| (0..info.width).map(move |x| (x, y)))
                .find(|&(x, y)| qj_result_hit(result, x, y) == 0)
                .unwrap()
        };
        let (x, y) = first_hit(base, small);
        let (bx, by) = first_hit(large, big);
        assert!(bx.abs_diff(x * 2) <= 2 && by.abs_diff(y * 2) <= 2);
        assert_eq!(qj_result_hit(large, bx + 4, by + 4), 0);
        for (ui, text) in [(0.74, 1.0), (2.01, 1.0), (1.0, f32::NAN), (1.0, 3.01)] {
            assert!(render(ui, text, SIZE_ABI_VERSION).is_null());
        }
        assert!(render(1.0, 1.0, SIZE_ABI_VERSION + 1).is_null());
        // 合法最大文字/UI偏好下空间充足，序号不得被固定24px预算裁掉。
        for layout in [LayoutMode::Vertical, LayoutMode::Horizontal] {
            let mut large_frame = frame.clone();
            large_frame.layout = layout;
            let large_json = serde_json::to_vec(&large_frame).unwrap();
            for text_scale in [2.0, 3.0] {
                let result = qj_renderer_render_sized(
                    handle,
                    ABI_VERSION,
                    SIZE_ABI_VERSION,
                    large_json.as_ptr(),
                    large_json.len(),
                    1.0,
                    2.0,
                    text_scale,
                    1600,
                    900,
                    0,
                );
                let mut info = ImageInfo::default();
                assert!(!result.is_null() && qj_result_image(result, &mut info));
                assert_eq!(info.truncated, 0);
                qj_result_destroy(result);
            }
        }
        for result in [legacy, base, large, text] {
            qj_result_destroy(result);
        }
        qj_renderer_destroy(handle);
    }
}
