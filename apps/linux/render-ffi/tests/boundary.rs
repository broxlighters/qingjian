//! C ABI 的所有权、空指针、版本、畸形输入和稀疏槽位回归。
use qingjian_platform::protocol::Frame;
use qingjian_render::{FontLibrary, Renderer};
use qingjian_render_ffi::{
    ABI_VERSION, ImageInfo, qj_renderer_clear_text_cache, qj_renderer_create, qj_renderer_destroy,
    qj_renderer_render, qj_result_destroy, qj_result_exposure, qj_result_hit, qj_result_image,
};
use std::ptr::{null, null_mut};

#[test]
fn malformed_boundaries_fail_and_results_outlive_renderer() {
    let renderer =
        Renderer::new(
            FontLibrary::from_fonts(
                [include_bytes!(
                    "../../../../crates/qingjian-render/tests/fonts/NotoSans-Regular.ttf"
                )
                .to_vec()],
                "Noto Sans",
                "zh-CN",
            )
            .unwrap(),
        );
    let handle = Box::into_raw(Box::new(renderer));
    let json = serde_json::to_vec(&Frame::default()).unwrap();
    unsafe {
        assert!(qj_renderer_create(ABI_VERSION + 1).is_null());
        assert!(
            qj_renderer_render(handle, 0, json.as_ptr(), json.len(), 1.0, 400, 300, 0).is_null()
        );
        assert!(qj_renderer_render(handle, ABI_VERSION, null(), 1, 1.0, 400, 300, 0).is_null());
        assert!(
            qj_renderer_render(
                handle,
                ABI_VERSION,
                json.as_ptr(),
                json.len(),
                f32::NAN,
                400,
                300,
                0
            )
            .is_null()
        );
        assert!(
            qj_renderer_render(
                handle,
                ABI_VERSION,
                json.as_ptr(),
                json.len(),
                1.0,
                0,
                300,
                0
            )
            .is_null()
        );
        assert!(
            qj_renderer_render(handle, ABI_VERSION, b"{".as_ptr(), 1, 1.0, 400, 300, 0).is_null()
        );
        assert!(!qj_result_image(null(), null_mut()));
        assert_eq!(qj_result_hit(null(), 0, 0), -1);
        let result = qj_renderer_render(
            handle,
            ABI_VERSION,
            json.as_ptr(),
            json.len(),
            1.0,
            400,
            300,
            0,
        );
        assert!(!result.is_null());
        qj_renderer_clear_text_cache(handle);
        qj_renderer_clear_text_cache(null_mut());
        qj_renderer_destroy(handle);
        let mut image = ImageInfo::default();
        assert!(qj_result_image(result, &mut image));
        assert_eq!(
            (image.width, image.height, image.stride, image.length),
            (1, 1, 4, 4)
        );
        assert_eq!(
            std::slice::from_raw_parts(image.pixels, image.length as usize),
            [0, 0, 0, 0]
        );
        assert_eq!(qj_result_hit(result, u32::MAX, u32::MAX), -1);
        let (mut row, mut sense) = (0, 0);
        assert!(!qj_result_exposure(result, 0, &mut row, &mut sense));
        qj_result_destroy(result);
        qj_result_destroy(null_mut());
        qj_renderer_destroy(null_mut());
    }
}
